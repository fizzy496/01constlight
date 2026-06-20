/**
 * @file    cuankou.c
 * @brief   UART 串口协议 — 双色温智能台灯 BLE 自定义通信协议
 * @details 帧格式: 0xAA + CMD(1字节) + DATA(可变) + 0xBB
 *
 *          CMD bit7 = 0: 无需应答 (高频调光)
 *          CMD bit7 = 1: 请求设备应答 (关键配置)
 *
 *          查询指令固定返回应答，不受应答位控制。
 *
 *          指令集:
 *            0x01 = 开关控制      (1字节: 0=关/1=开)
 *            0x02 = 亮度设置      (1字节: 0~100%)
 *            0x03 = 色温设置      (2字节: uint16, 3000~6500K)
 *            0x04 = 定时开关机    (8字节)
 *            0x05 = 恢复出厂设置  (0字节)
 *            0x06 = 倒计时关灯    (2字节: uint16, 0~1440分钟)
 *            0x0A = OTA升级开始   (11字节)
 *            0x0B = OTA固件数据   (可变)
 *            0x0C = OTA传输结束   (2字节)
 *            0x0D = OTA升级取消   (0字节)
 *            0x10 = 查询当前状态  (0字节) → 应答 0x20
 *            0x11 = 查询设备信息  (0字节) → 应答 0x21
 *            0x12 = 查询定时配置  (1字节) → 应答 0x22
 *            0x13 = 查询倒计时    (0字节) → 应答 0x23
 *            0x14 = 查询OTA状态   (0字节) → 应答 0x24
 */
#include "cuankou.h"
#include "dac_ctrl.h"
#include "usart.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ====================== 协议常量 ====================== */
#define FRAME_HEAD      0xAA        // 帧头
#define FRAME_TAIL      0xBB        // 帧尾
#define RX_BUF_MAX      260         // 接收缓冲区 (OTA数据包最大240+帧头+CMD+帧尾)
#define STATUS_OK       0x00        // 执行成功
#define STATUS_PARAM_ERR 0x01       // 参数错误
#define STATUS_STATE_ERR 0x02       // 状态不允许
#define STATUS_UNKNOWN   0x03       // 未知指令

/* ====================== 帧解析状态机 ====================== */
typedef enum {
    RX_IDLE,        // 等待帧头 0xAA
    RX_CMD,         // 等待指令码
    RX_DATA,        // 接收数据域
    RX_DONE         // 收到帧尾, 待处理
} RxState_T;

/* ====================== OTA 升级状态 ====================== */
#define OTA_IDLE        0x00
#define OTA_PREPARE     0x01
#define OTA_TRANSFER    0x02
#define OTA_DONE_WAIT   0x03
#define OTA_FAILED      0x04

/* ====================== 局部变量 ====================== */
static uint8_t  rx_buf[RX_BUF_MAX];     // 接收缓冲区
static uint16_t rx_cnt;                 // 当前接收字节数
static RxState_T rx_state = RX_IDLE;    // 当前解析状态
static uint8_t  rx_cmd;                 // 当前指令码
static uint8_t  rx_data_len;            // 预期数据长度

/* ====================== 应答队列 (ISR → 主循环, 避免 ISR 中阻塞发送) ====================== */
#define RSP_QUEUE_SIZE  4
#define RSP_MAX_LEN     16
static uint8_t  rsp_queue[RSP_QUEUE_SIZE][RSP_MAX_LEN];
static uint8_t  rsp_queue_len[RSP_QUEUE_SIZE];
static volatile uint8_t rsp_wr = 0;     // ISR 写入索引
static uint8_t  rsp_rd = 0;             // 主循环读取索引

/* ====================== 各指令约定数据长度 ====================== */
static const uint8_t cmd_data_len[0x15] = {
    [0x01] = 1,      // 开关控制
    [0x02] = 1,      // 亮度设置
    [0x03] = 2,      // 色温设置
    [0x04] = 8,      // 定时开关机
    [0x05] = 0,      // 恢复出厂设置
    [0x06] = 2,      // 倒计时关灯
    [0x0A] = 11,     // OTA升级开始
    [0x0B] = 0xFF,   // OTA固件数据 (可变长度, 特殊处理)
    [0x0C] = 2,      // OTA传输结束
    [0x0D] = 0,      // OTA升级取消
    [0x10] = 0,      // 查询当前状态
    [0x11] = 0,      // 查询设备信息
    [0x12] = 1,      // 查询定时配置
    [0x13] = 0,      // 查询倒计时
    [0x14] = 0,      // 查询OTA状态
};

/* ====================== 辅助函数 ====================== */

/**
 * @brief  发送单字节 (阻塞)
 */
static void uart_send_byte(uint8_t byte)
{
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = byte;
}

/**
 * @brief  入队应答帧 (ISR 安全, 无阻塞)
 * @note   将应答数据拷贝到队列, 由主循环 rsp_flush() 发送
 */
static void rsp_enqueue(const uint8_t *data, uint8_t len)
{
    if (len > RSP_MAX_LEN) return;
    uint8_t next = (rsp_wr + 1) % RSP_QUEUE_SIZE;
    if (next == rsp_rd) return;  // 队列满, 丢弃 (不应发生)
    for (uint8_t i = 0; i < len; i++)
        rsp_queue[rsp_wr][i] = data[i];
    rsp_queue_len[rsp_wr] = len;
    rsp_wr = next;
}

/**
 * @brief  发送应答帧 (入队, 不阻塞)
 */
static void send_response(uint8_t cmd, uint8_t status)
{
    uint8_t rsp[4];
    rsp[0] = FRAME_HEAD;
    rsp[1] = cmd;
    rsp[2] = status;
    rsp[3] = FRAME_TAIL;
    rsp_enqueue(rsp, 4);
}

/**
 * @brief  发送查询应答帧 (入队, 不阻塞)
 */
static void send_query_response(uint8_t rsp_cmd, const uint8_t *data, uint8_t len)
{
    uint8_t rsp[RSP_MAX_LEN];
    rsp[0] = FRAME_HEAD;
    rsp[1] = rsp_cmd;
    uint8_t total = 2;
    for (uint8_t i = 0; i < len && total < RSP_MAX_LEN - 1; i++)
        rsp[total++] = data[i];
    rsp[total++] = FRAME_TAIL;
    rsp_enqueue(rsp, total);
}

/**
 * @brief  主循环调用: 发送队列中的所有应答帧
 * @note   阻塞发送 (在主循环中安全)
 */
void rsp_flush(void)
{
    while (rsp_rd != rsp_wr)
    {
        uint8_t len = rsp_queue_len[rsp_rd];
        for (uint8_t i = 0; i < len; i++)
        {
            while (!(USART1->SR & USART_SR_TXE));
            USART1->DR = rsp_queue[rsp_rd][i];
        }
        while (!(USART1->SR & USART_SR_TC));
        rsp_rd = (rsp_rd + 1) % RSP_QUEUE_SIZE;
    }
}

/**
 * @brief  读取2字节大端uint16
 */
static uint16_t read_uint16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/**
 * @brief  写入2字节大端uint16
 */
static void write_uint16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFF);
}

/**
 * @brief  写入4字节大端uint32
 */
static void write_uint32_be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val & 0xFF);
}

/* ====================== 指令处理 ====================== */

/**
 * @brief  命令 0x01: 开关控制
 * @param  data[0]: 0x00=关灯, 0x01=开灯
 */
static void cmd_power(uint8_t *data, uint8_t need_ack)
{
    if (data[0] > 1) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }

    if (data[0] == 0x00) {
        /* 关灯: 先设状态再调DAC，DAC_set() 检测 lamp_state==0 输出关灯电压 */
        g_lamp.lamp_state = 0;
        g_lamp.color_temp_mode = 0;
        g_lamp.save_brightness = 1;
        DAC_set(0);
        DAC_set(1);
    } else {
        /* 开灯: 先恢复亮度再设状态，DAC_set() 输出记忆亮度 */
        if (g_lamp.brightness[0] == 0) g_lamp.brightness[0] = 50;
        if (g_lamp.brightness[1] == 0) g_lamp.brightness[1] = 50;
        g_lamp.lamp_state = 1;
        g_lamp.save_brightness = 1;
        DAC_set(0);
        DAC_set(1);
    }

    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x02: 亮度设置
 * @param  data[0]: 亮度值 0~100 (%)
 */
static void cmd_brightness(uint8_t *data, uint8_t need_ack)
{
    if (data[0] > 100) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }

    g_lamp.brightness[0] = data[0];
    g_lamp.brightness[1] = data[0];
    g_lamp.color_temp_mode = 0;
    // 亮度0 = 最低亮度 (微光)，不关灯；关灯由 power 指令 (0x01) 控制
    g_lamp.lamp_state = 1;

    DAC_set(0);
    DAC_set(1);

    g_lamp.save_brightness = 1;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x03: 色温设置
 * @param  data[0:1]: uint16 色温值 3000~6500K
 */
static void cmd_color_temp(uint8_t *data, uint8_t need_ack)
{
    uint16_t kelvin = read_uint16_be(data);
    if (kelvin < 3000 || kelvin > 6500) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }

    color_temp_apply(kelvin);
    g_lamp.lamp_state = 1;

    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x04: 定时开关机配置
 * @param  data[0]: 定时ID (0=开, 1=关)
 *         data[1]: 使能 (0=禁用, 1=启用)
 *         data[2]: 小时 (0~23)
 *         data[3]: 分钟 (0~59)
 *         data[4]: 星期位图 (bit0=周一...bit6=周日)
 *         data[5]: 亮度 (0~100)
 *         data[6:7]: 色温 uint16 (3000~6500K)
 */
static void cmd_timer(uint8_t *data, uint8_t need_ack)
{
    uint8_t tid = data[0];
    if (tid > 1) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }
    if (data[2] > 23 || data[3] > 59) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }

    g_lamp.timer_enable[tid]      = data[1];
    g_lamp.timer_hour[tid]        = data[2];
    g_lamp.timer_minute[tid]      = data[3];
    g_lamp.timer_weekday[tid]     = data[4];
    g_lamp.timer_brightness[tid]  = data[5];
    g_lamp.timer_color_temp[tid]  = read_uint16_be(&data[6]);

    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x05: 恢复出厂设置
 */
static void cmd_factory_reset(uint8_t *data, uint8_t need_ack)
{
    (void)data;

    /* 恢复默认亮度 */
    g_lamp.brightness[0] = 50;
    g_lamp.brightness[1] = 50;
    g_lamp.lamp_state = 0;
    g_lamp.color_temp_mode = 0;
    g_lamp.color_temp_k = 4000;

    /* 清除定时器 */
    memset(g_lamp.timer_enable, 0, sizeof(g_lamp.timer_enable));

    /* 清除倒计时 */
    g_lamp.countdown_minutes = 0;

    DAC_set(0);
    DAC_set(1);

    g_lamp.save_brightness = 1;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x06: 倒计时关灯设置
 * @param  data[0:1]: uint16 分钟数 (0=取消, 1~1440)
 */
static void cmd_countdown(uint8_t *data, uint8_t need_ack)
{
    uint16_t minutes = read_uint16_be(data);
    if (minutes > 1440) {
        if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
        return;
    }

    g_lamp.countdown_minutes = minutes;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x0A: OTA升级开始
 */
static void cmd_ota_start(uint8_t *data, uint8_t need_ack)
{
    (void)data;
    g_lamp.ota_state = OTA_PREPARE;
    g_lamp.ota_error = 0;
    g_lamp.ota_packets_received = 0;
    g_lamp.ota_bytes_received = 0;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x0B: OTA固件数据包
 */
static void cmd_ota_data(uint8_t *data, uint8_t len, uint8_t need_ack)
{
    if (g_lamp.ota_state != OTA_PREPARE && g_lamp.ota_state != OTA_TRANSFER) {
        if (need_ack) send_response(rx_cmd, STATUS_STATE_ERR);
        return;
    }

    g_lamp.ota_state = OTA_TRANSFER;
    g_lamp.ota_packets_received++;
    g_lamp.ota_bytes_received += len;

    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x0C: OTA传输结束
 */
static void cmd_ota_end(uint8_t *data, uint8_t need_ack)
{
    (void)data;
    g_lamp.ota_state = OTA_DONE_WAIT;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/**
 * @brief  命令 0x0D: OTA升级取消
 */
static void cmd_ota_cancel(uint8_t *data, uint8_t need_ack)
{
    (void)data;
    g_lamp.ota_state = OTA_IDLE;
    g_lamp.ota_error = 0;
    if (need_ack) send_response(rx_cmd, STATUS_OK);
}

/* ====================== 查询指令 ====================== */

/**
 * @brief  查询 0x10: 当前状态 → 应答 0x20
 * @note   应答: 开关状态(1) + 亮度(1) + 色温uint16(2) = 4字节
 */
static void query_status(void)
{
    uint8_t rsp[4];
    rsp[0] = g_lamp.lamp_state;
    rsp[1] = g_lamp.brightness[0];
    write_uint16_be(&rsp[2], g_lamp.color_temp_k ? g_lamp.color_temp_k : 4000);
    send_query_response(0x20, rsp, 4);
}

/**
 * @brief  查询 0x11: 设备信息 → 应答 0x21
 * @note   应答: 主版本(1) + 次版本(1) + 修订版(1) + 温度int16(2) = 5字节
 */
static void query_device_info(void)
{
    uint8_t rsp[5];
    rsp[0] = g_config.version_major;
    rsp[1] = g_config.version_minor;
    rsp[2] = 0;     // 修订版本号
    write_uint16_be(&rsp[3], (uint16_t)g_lamp.temperature);
    send_query_response(0x21, rsp, 5);
}

/**
 * @brief  查询 0x12: 定时配置 → 应答 0x22
 * @note   请求: 定时ID(1字节)
 *         应答: 8字节定时配置 (与设置指令格式一致)
 */
static void query_timer(uint8_t *data)
{
    uint8_t tid = data[0];
    if (tid > 1) {
        send_response(0x92, STATUS_PARAM_ERR);  // bit7=1
        return;
    }

    uint8_t rsp[8];
    rsp[0] = tid;
    rsp[1] = g_lamp.timer_enable[tid];
    rsp[2] = g_lamp.timer_hour[tid];
    rsp[3] = g_lamp.timer_minute[tid];
    rsp[4] = g_lamp.timer_weekday[tid];
    rsp[5] = g_lamp.timer_brightness[tid];
    write_uint16_be(&rsp[6], g_lamp.timer_color_temp[tid]);
    send_query_response(0x22, rsp, 8);
}

/**
 * @brief  查询 0x13: 倒计时剩余时间 → 应答 0x23
 * @note   应答: uint16 分钟数
 */
static void query_countdown(void)
{
    uint8_t rsp[2];
    write_uint16_be(rsp, g_lamp.countdown_minutes);
    send_query_response(0x23, rsp, 2);
}

/**
 * @brief  查询 0x14: OTA状态 → 应答 0x24
 * @note   应答: 状态(1) + 错误码(1) + 已收包数uint16(2) + 已收字节数uint32(4) = 8字节
 */
static void query_ota_status(void)
{
    uint8_t rsp[8];
    rsp[0] = g_lamp.ota_state;
    rsp[1] = g_lamp.ota_error;
    write_uint16_be(&rsp[2], g_lamp.ota_packets_received);
    write_uint32_be(&rsp[4], g_lamp.ota_bytes_received);
    send_query_response(0x24, rsp, 8);
}

/* ====================== 帧处理 ====================== */

/**
 * @brief  处理完整帧
 */
static void process_frame(void)
{
    uint8_t  func_code = rx_cmd & 0x7F;     // 低7位功能码
    uint8_t  need_ack  = (rx_cmd & 0x80) ? 1 : 0;  // bit7应答位
    uint8_t *data      = rx_buf;            // 数据域起始
    uint8_t  data_len  = rx_cnt;            // 实际数据长度

    /* 查询指令固定应答，忽略应答位 */
    if (func_code >= 0x10 && func_code <= 0x14) {
        switch (func_code) {
            case 0x10: query_status(); break;
            case 0x11: query_device_info(); break;
            case 0x12:
                if (data_len >= 1) query_timer(data);
                break;
            case 0x13: query_countdown(); break;
            case 0x14: query_ota_status(); break;
            default: break;
        }
        return;
    }

    /* 可变长度指令 (0x0B OTA数据) 不做长度校验 */
    if (func_code != 0x0B) {
        uint8_t expected = cmd_data_len[func_code];
        if (data_len != expected) {
            if (need_ack) send_response(rx_cmd, STATUS_PARAM_ERR);
            return;
        }
    }

    /* 控制指令分派 */
    switch (func_code) {
        case 0x01: cmd_power(data, need_ack);            break;
        case 0x02: cmd_brightness(data, need_ack);       break;
        case 0x03: cmd_color_temp(data, need_ack);       break;
        case 0x04: cmd_timer(data, need_ack);            break;
        case 0x05: cmd_factory_reset(data, need_ack);    break;
        case 0x06: cmd_countdown(data, need_ack);        break;
        case 0x0A: cmd_ota_start(data, need_ack);        break;
        case 0x0B: cmd_ota_data(data, data_len, need_ack); break;
        case 0x0C: cmd_ota_end(data, need_ack);          break;
        case 0x0D: cmd_ota_cancel(data, need_ack);       break;
        default:
            if (need_ack) send_response(rx_cmd, STATUS_UNKNOWN);
            break;
    }
}

/* ====================== UART RX 回调 ====================== */

/**
 * @brief  串口接收回调 — 帧解析状态机
 * @note   由 USART1_IRQHandler 或主循环调用，每次传入一个字节
 *
 *         状态机流程:
 *           RX_IDLE  → 收到 0xAA → RX_CMD
 *           RX_CMD   → 收到 CMD   → RX_DATA (已知数据长度)
 *           RX_DATA  → 收数据     → 收到 0xBB → RX_DONE → process_frame()
 */
void callback_232(uint8_t byte)
{
    switch (rx_state) {
        case RX_IDLE:
            if (byte == FRAME_HEAD) {
                rx_state = RX_CMD;
                rx_cnt = 0;
            }
            break;

        case RX_CMD:
            rx_cmd = byte;
            rx_state = RX_DATA;
            rx_cnt = 0;

            /* 查询固定长度指令: 若数据长度为0则直接等帧尾 */
            {
                uint8_t func = rx_cmd & 0x7F;
                rx_data_len = (func < 0x15) ? cmd_data_len[func] : 0xFF;
            }
            break;

        case RX_DATA:
            if (byte == FRAME_TAIL) {
                /* 固定长度指令: 校验数据长度 */
                {
                    uint8_t func = rx_cmd & 0x7F;
                    if (func != 0x0B && rx_cnt != rx_data_len) {
                        /* 长度不匹配, 丢弃 */
                        rx_state = RX_IDLE;
                        break;
                    }
                }
                rx_state = RX_IDLE;
                process_frame();
            } else {
                if (rx_cnt < RX_BUF_MAX) {
                    rx_buf[rx_cnt++] = byte;
                } else {
                    /* 缓冲区溢出, 丢弃 */
                    rx_state = RX_IDLE;
                }
            }
            break;

        default:
            rx_state = RX_IDLE;
            break;
    }
}

/* ====================== 定时状态上报 ====================== */

/**
 * @brief  主动上报当前状态 (每秒调用)
 * @note   格式: 0xAA + 0x20 + 开关(1) + 亮度(1) + 色温uint16(2) + 0xBB
 *         与查询 0x10 的应答格式一致
 */
void Status_Report_Periodic(void)
{
    uint8_t rsp[4];
    rsp[0] = g_lamp.lamp_state;
    rsp[1] = g_lamp.brightness[0];
    write_uint16_be(&rsp[2], g_lamp.color_temp_k ? g_lamp.color_temp_k : 4000);
    send_query_response(0x20, rsp, 4);
}
