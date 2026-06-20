/**
 * @file    public.h
 * @brief   公共头文件 — 全局配置结构体、设备运行时状态、通用宏
 */
#ifndef __PUBLIC_H
#define __PUBLIC_H

#include "main.h"
#include "tim.h"
#include "dma.h"
#include "usart.h"
#include "dac.h"
#include "iwdg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 项目配置结构体 ====================== */
typedef struct {
    /* ---- 电流范围 (mA) ---- */
    uint16_t current_max_mA;                // 最大输出电流 (mA)，亮度99对应，默认 302
    uint16_t current_min_mA;                // 最小可亮电流 (mA)，亮度1对应，默认 10
    uint16_t current_off_mA;                // 关灯阈值电流 (mA)，低于此值视为关灯

    /* ---- DAC 电气参数 ---- */
    uint16_t dac_vref_mV;                   // DAC 参考电压 (mV)，默认 3300
    uint16_t dac_resolution;                // DAC 分辨率 (12位 = 4095)

    /* ---- 软启动 / 软关断 ---- */
    uint8_t  fade_steps;                    // 渐变步数，默认 20

    /* ---- 温度保护 ---- */
    int16_t  temp_alarm_trigger;            // 过温报警触发阈值 (x10，如 900 = 90.0°C)
    int16_t  temp_alarm_reset;              // 过温报警复位阈值 (x10)

    /* ---- 风扇 PWM ---- */
    uint8_t  fan_pwm_arr;                   // TIM2 自动重装载值 (254)
    uint8_t  fan_temp_t1;                   // 风扇转速 T1~T4 阈值 (x10)
    uint8_t  fan_temp_t2;
    uint8_t  fan_temp_t3;
    uint8_t  fan_temp_t4;

    /* ---- 版本号 ---- */
    uint8_t  version_major;
    uint8_t  version_minor;

    /* ---- 定时周期 (ms) ---- */
    uint16_t tick_fade_ms;
    uint16_t tick_fan_ms;
    uint16_t tick_alarm_ms;
} Project_Config_T;

/* ====================== 设备运行时状态结构体 ====================== */
typedef struct {
    /* ---- 灯光控制 (2 通道: 白光 + 黄光) ---- */
    uint8_t  lamp_state;
    uint8_t  brightness[2];
    uint8_t  save_brightness;

    /* ---- 色温混合 ---- */
    uint8_t  color_temp_mode;               // 0=原始亮度, 1=色温模式
    uint16_t color_temp_k;                  // 色温值 (2700~6500K)

    /* ---- DAC 校准数据 ---- */
    uint16_t dark[2];                       // 暗校准 DAC 码值
    uint16_t bright[2];                     // 亮校准 DAC 码值
    uint8_t  save_dark[2];                  // 暗校准脏标记
    uint8_t  save_bright[2];                // 亮校准脏标记

    /* ---- 温度 ---- */
    int16_t  temperature;                   // 最新温度读数 (x10)
    int16_t  temp_recent[10];               // 温度环形缓冲区
    uint8_t  temp_alarm;                    // 0=正常, 1=报警中

    /* ---- 系统滴答 ---- */
    uint32_t tick;                          // 毫秒级滴答计数器

    /* ---- 定时开关机 (持久化) ---- */
    uint8_t  timer_enable[2];               // 0=定时开, 1=定时关
    uint8_t  timer_hour[2];                 // 触发小时 (0~23)
    uint8_t  timer_minute[2];               // 触发分钟 (0~59)
    uint8_t  timer_weekday[2];              // 星期重复位图
    uint8_t  timer_brightness[2];           // 触发亮度 (仅定时开)
    uint16_t timer_color_temp[2];           // 触发色温 (仅定时开)

    /* ---- 倒计时关灯 (掉电丢失) ---- */
    uint16_t countdown_minutes;             // 剩余分钟数, 0=无倒计时

    /* ---- OTA 升级状态 ---- */
    uint8_t  ota_state;
    uint8_t  ota_error;
    uint16_t ota_packets_received;
    uint32_t ota_bytes_received;
} Lamp_Device_T;

/* ====================== 全局实例 ====================== */
extern const Project_Config_T g_config;
extern Lamp_Device_T         g_lamp;

/* ====================== 便捷宏 ====================== */
#define FAN_T1  (g_config.fan_temp_t1)
#define FAN_T2  (g_config.fan_temp_t2)
#define FAN_T3  (g_config.fan_temp_t3)
#define FAN_T4  (g_config.fan_temp_t4)

#ifdef __cplusplus
}
#endif

#endif /* __PUBLIC_H */
