/**
 * @file    lamp.h
 * @brief   台灯核心功能 — 初始化、关灯、温度监测、PVD掉电保护
 */

#ifndef __LAMP_H
#define __LAMP_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 防爆闪 — 上电短暂导通 MOS 防止电流冲击 ====================== */
#define Burst_Flash  do { GPIOA->BSRR = GPIO_PIN_7; HAL_Delay(5); GPIOA->BRR = GPIO_PIN_7; } while(0)

/* ====================== 函数声明 ====================== */
void init(void);
void ledoff(void);
void temperature_read(void);

#ifdef __cplusplus
}
#endif

#endif /* __LAMP_H */
