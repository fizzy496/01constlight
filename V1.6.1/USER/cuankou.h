/**
 * @file    cuankou.h
 * @brief   UART 串口协议 — BLE 自定义通信协议
 * @details 帧格式: 0xAA + CMD(1字节) + DATA(可变) + 0xBB
 */

#ifndef __CUANKOU_H
#define __CUANKOU_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== 函数声明 ====================== */
void callback_232(uint8_t byte);
void Status_Report_Periodic(void);
void rsp_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* __CUANKOU_H */