/**
 * @file    keydisp.c
 * @brief   旋转编码器驱动 — 亮度调节 + 开关控制
 * @note    编码器: A=PB12, B=PB13, SW(按键)=PC6
 *          旋转方向: CW=增亮, CCW=减亮
 *          SW 短按: 开关灯
 *          SW 长按(>2s): 色温切换 (白光/黄光/混合)
 *          已移除 TM1650 数码管显示 (硬件取消)
 */


#include "dac_ctrl.h"

/* ====================== 编码器引脚 (无 tm1650.h 时直接定义) ====================== */
#define ENC_A   (GPIOB->IDR & GPIO_PIN_12)      // 编码器 A 相 PB12
#define ENC_B   (GPIOB->IDR & GPIO_PIN_13)      // 编码器 B 相 PB13
#define ENC_SW  (GPIOC->IDR & GPIO_PIN_6)       // 编码器按键 PC6

/* ====================== 编码器常量 ====================== */
#define ENC_DEBOUNCE_MS     20      // 消抖时间 (ms)
#define SW_LONG_PRESS_MS    2000    // 长按时间 (ms)
#define SW_DEBOUNCE_MS      50      // 按键消抖 (ms)
#define BRIGHTNESS_STEP     2       // 每格亮度步进

/* ====================== 编码器状态 ====================== */
static uint8_t  enc_last_a;             // 上次 A 相状态
static uint32_t enc_last_tick;          // 上次旋转时间
static uint32_t sw_press_start;         // 按键按下时刻
static uint8_t  sw_last_state;          // 上次按键状态
static uint8_t  sw_pressed;             // 按键当前是否按下
static uint8_t  sw_handled;             // 本次按下是否已处理

/* ====================== 初始化 ====================== */
void encoder_init(void)
{
    enc_last_a    = (ENC_A != 0) ? 1 : 0;
    enc_last_tick = 0;
    sw_press_start = 0;
    sw_last_state = (ENC_SW != 0) ? 1 : 0;
    sw_pressed    = 0;
    sw_handled    = 0;
}

/* ====================== 编码器扫描 (每 5ms 调用) ====================== */
void encoder_scan(void)
{
    uint32_t now = g_lamp.tick;
    uint8_t a = (ENC_A != 0) ? 1 : 0;
    uint8_t b = (ENC_B != 0) ? 1 : 0;
    uint8_t sw = (ENC_SW != 0) ? 1 : 0;

    /* ---- 旋转检测: A 相跳变时判断方向 ---- */
    if (a != enc_last_a && (now - enc_last_tick) >= ENC_DEBOUNCE_MS)
    {
        enc_last_tick = now;

        if (a != b)  // A 跳变时 A≠B → 顺时针 (CW)
        {
            /* 增亮 */
            if (g_lamp.lamp_state == 0)
            {
                /* 关机状态下旋转 → 开灯到最低亮度 */
                g_lamp.lamp_state = 1;
                g_lamp.brightness[0] = 1;
                g_lamp.brightness[1] = 1;
            }
            else
            {
                if (g_lamp.brightness[0] < 99)
                    g_lamp.brightness[0] += BRIGHTNESS_STEP;
                if (g_lamp.brightness[0] > 99)
                    g_lamp.brightness[0] = 99;
                g_lamp.brightness[1] = g_lamp.brightness[0];
            }
            g_lamp.color_temp_mode = 0;
            g_lamp.save_brightness = 1;
            DAC_set(0);
            DAC_set(1);
        }
        else  // A 跳变时 A==B → 逆时针 (CCW)
        {
            /* 减亮 */
            if (g_lamp.lamp_state == 0)
            {
                /* 关机状态下旋转 → 开灯到最低亮度 */
                g_lamp.lamp_state = 1;
                g_lamp.brightness[0] = 1;
                g_lamp.brightness[1] = 1;
            }
            else
            {
                if (g_lamp.brightness[0] > BRIGHTNESS_STEP)
                    g_lamp.brightness[0] -= BRIGHTNESS_STEP;
                else
                    g_lamp.brightness[0] = 0;   // 最低亮度 (微光)
                g_lamp.brightness[1] = g_lamp.brightness[0];
            }
            g_lamp.color_temp_mode = 0;
            g_lamp.save_brightness = 1;
            DAC_set(0);
            DAC_set(1);
        }
    }
    enc_last_a = a;

    /* ---- 按键检测: 短按开关灯, 长按切换色温 ---- */
    if (sw == 0 && sw_last_state == 1)  // 按下沿
    {
        /* 消抖 */
        if ((now - sw_press_start) < SW_DEBOUNCE_MS)
        {
            sw_last_state = sw;
            return;
        }
        sw_pressed = 1;
        sw_handled = 0;
        sw_press_start = now;
    }
    else if (sw == 0 && sw_pressed && !sw_handled)
    {
        /* 检测长按: >2秒 */
        if ((now - sw_press_start) >= SW_LONG_PRESS_MS)
        {
            sw_handled = 1;
            /* 长按: 色温切换 (白光 → 暖光 → 中性光 → 循环) */
            if (g_lamp.lamp_state == 0)
            {
                g_lamp.lamp_state = 1;
                g_lamp.brightness[0] = 50;
                g_lamp.brightness[1] = 50;
            }
            g_lamp.color_temp_mode = 1;

            if (g_lamp.color_temp_k <= 3000)
                g_lamp.color_temp_k = 4600;     // 暖光 → 中性光
            else if (g_lamp.color_temp_k >= 6500)
                g_lamp.color_temp_k = 3000;     // 冷光 → 暖光
            else if (g_lamp.color_temp_k >= 4600)
                g_lamp.color_temp_k = 6500;     // 中性光 → 冷光
            else
                g_lamp.color_temp_k = 4600;

            color_temp_apply(g_lamp.color_temp_k);
            g_lamp.save_brightness = 1;
        }
    }
    else if (sw == 1 && sw_last_state == 0)  // 释放沿
    {
        if (sw_pressed && !sw_handled)
        {
            /* 短按: 开关灯 */
            if (g_lamp.lamp_state == 0)
            {
                /* 开灯: 恢复上次亮度 */
                if (g_lamp.brightness[0] == 0) g_lamp.brightness[0] = 50;
                if (g_lamp.brightness[1] == 0) g_lamp.brightness[1] = 50;
                g_lamp.lamp_state = 1;
            }
            else
            {
                /* 关灯: 进入慢速关机渐变 */
                g_lamp.lamp_state = 0;
            }
            g_lamp.save_brightness = 1;
            DAC_set(0);
            DAC_set(1);
        }
        sw_pressed = 0;
        sw_handled = 0;
    }
    sw_last_state = sw;
}
