/**
 * @file    DS18B20.c
 * @brief   DS18B20 温度传感器驱动 (单总线接口)
 * @note    DQ = PB1, 精度 0.1°C, 范围 -55°C ~ +125°C
 */

#include "ds18b20.h"

/* ====================== 引脚定义 ====================== */
#define DQ1_1   (GPIOB->BSRR = GPIO_PIN_1)   // DQ 拉高
#define DQ1_0   (GPIOB->BRR  = GPIO_PIN_1)   // DQ 拉低
#define DQ1     ((GPIOB->IDR & GPIO_PIN_1) && 0x01)  // 读取 DQ

#define d   9   // 延时基准: 64MHz 时约 1us/loop

static uint8_t k;
 
uint8_t DS18B20_Rst(void)
{
    uint8_t retry = 0;

    /* 复位脉冲: 拉低 480us */
    DQ1_1;
    DQ1_0;
    k = 480 * d; while (--k);

    /* 释放总线, 等待 15us */
    DQ1_1;
    k = 15 * d; while (--k);

    /* 等待从机拉低总线 (应答) */
    while (DQ1 && retry < 200)
    {
        retry++;
        k = d; while (--k);
    }
    if (retry >= 200) return 1;
    else retry = 0;

    /* 等待从机释放总线 */
    while (!DQ1 && retry < 240)
    {
        retry++;
        k = d; while (--k);
    }
    if (retry >= 240) return 1;

    return 0;
}

/* ====================== 读一个字节 ====================== */
/**
 * @brief  从 DS18B20 读取一个字节 (LSB first)
 */
uint8_t DS18B20_Read_Byte(void)
{
    uint8_t i;
    uint8_t value = 0;

    for (i = 8; i > 0; i--)
    {
        value >>= 1;
        DQ1_0;
        k = 6 * d; while (--k);     // 拉低 6us
        DQ1_1;                      // 释放总线
        k = 4 * d; while (--k);     // 等待 4us
        if (DQ1) value |= 0x80;     // 采样
        k = 60 * d; while (--k);    // 等待 63us 完成时隙
    }
    return value;
}

/* ====================== 写一个字节 ====================== */
/**
 * @brief  向 DS18B20 写入一个字节 (LSB first)
 */
void DS18B20_Write_Byte(uint8_t val)
{
    uint8_t i;
    uint8_t temp;

    for (i = 8; i > 0; i--)
    {
        temp = val & 0x01;          // 取最低位
        DQ1_0;
        k = 2 * d; while (--k);     // 拉低 2us, 开始写时隙
        if (temp == 1) DQ1_1;       // 写 1: 拉高总线
        k = 60 * d; while (--k);     // 等待 63us
        DQ1_1;
        k = 2 * d; while (--k);
        val = val >> 1;             // 移位到下一位
    }
}

/* ====================== 启动温度转换 ====================== */
void DS18B20_convert(void)
{
    DS18B20_Rst();
    k = 2 * d; while (--k);
    DS18B20_Write_Byte(0xCC);   // SKIP ROM
    DS18B20_Write_Byte(0x44);   // CONVERT T
}

/* ====================== 读取温度 ====================== */
/**
 * @brief  读取 DS18B20 温度值
 * @return 温度值 (单位: 0.1°C), 范围 -550 ~ 1250
 */
short DS18B20_Get(void)
{
    uint8_t temp;
    uint8_t TL, TH;
    short tem;

    DS18B20_Rst();
    k = 2 * d; while (--k);

    DS18B20_Write_Byte(0xCC);   // SKIP ROM
    DS18B20_Write_Byte(0xBE);   // READ SCRATCHPAD

    TL = DS18B20_Read_Byte();   // 低字节
    TH = DS18B20_Read_Byte();   // 高字节

    /* 判断正负: TH > 7 表示负温度 */
    if (TH > 7)
    {
        TH = ~TH;
        TL = ~TL;
        temp = 0;               // 负温度
    }
    else
    {
        temp = 1;               // 正温度
    }

    tem = TH;                   // 高 8 位
    tem <<= 8;
    tem += TL;                  // 低 8 位
    tem = (float)tem * 0.625;   // 转换为 0.1°C 单位

    if (temp)
        return tem;             // 返回正温度
    else
        return -tem;            // 返回负温度
}
