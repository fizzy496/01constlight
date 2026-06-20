/**
 * @file    ds18b20.h
 * @brief   DS18B20 温度传感器驱动 (单总线接口, DQ=PB1)
 */

#ifndef __DS18B20_H
#define __DS18B20_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 函数声明 ====================== */
void  DS18B20_convert(void);
short DS18B20_Get(void);

#ifdef __cplusplus
}
#endif

#endif /* __DS18B20_H */
