/**
 * @file    fan.h
 * @brief   PWM 风扇控制 (TIM2 CH1)
 */

#ifndef __FAN_H
#define __FAN_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 风扇开关宏 ====================== */
#define Fan_open    (TIM2->CCER |= 0x0001)
#define Fan_close   (TIM2->CCER &= 0xFFFE)

/* ====================== 函数声明 ====================== */
void fan_set(uint8_t fan_temp);

#ifdef __cplusplus
}
#endif

#endif /* __FAN_H */
