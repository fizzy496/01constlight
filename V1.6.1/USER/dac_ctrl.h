/**
 * @file    dac_ctrl.h
 * @brief   DAC 控制 — 软渐变、校准、色温混合
 * @note    电流-电压反比关系: V = 1.30122 - 0.00039671 * ImA
 */

#ifndef __DAC_CTRL_H
#define __DAC_CTRL_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 函数声明 ====================== */

/* 底层硬件写入 */
void     DAC_SetVoltage_Int(uint32_t dac_channel, uint16_t voltage_mv);

/* 软渐变设置 */
void     DAC_set(uint8_t channel_idx);
void     DAC_set_instant(uint8_t channel_idx);
void     DAC_fade_tick(void);
uint8_t  dac_fade_is_idle(void);

/* 电流-电压转换 */
double   mA_to_V(double ImA);
double   V_to_I(double U);

/* 亮度转换链 */
uint16_t brightness_to_current_mA(uint8_t brightness);
uint16_t brightness_to_dac_code(uint8_t brightness);

/* 硬件读取 */
uint16_t dac_get_current_code(uint8_t channel_idx);
uint16_t dac_code_to_mV(uint16_t code);

/* 色温混合 */
void     color_temp_apply(uint16_t kelvin);

#ifdef __cplusplus
}
#endif

#endif /* __DAC_CTRL_H */
