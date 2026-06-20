/**
 * @file    main.c
 * @brief   主入口 - LED 恒流驱动 (白光 + 黄光)
 * @details STM32F103RCT6 @72MHz
 *          DAC1=PA4 (白光), DAC2=PA5 (黄光)
 *          UART 协议, DS18B20 温度传感器, 内部 Flash 存储
 *          PVD 掉电检测, IWDG 看门狗
 *
 * @note    上电流程:
 *           1. HAL 初始化 + 时钟配置
 *           2. 外设初始化 (GPIO, DMA, DAC, USART, IWDG)
 *           3. 应用初始化 -> 从内部 Flash 恢复参数或加载默认值
 *           4. 使能 PVD 掉电检测
 *           5. 主循环
 *
 *          主循环定时:
 *           5ms   - DAC 渐变时钟推进
 *           100ms - 风扇控制
 *           1000ms- 定时状态上报
 */

#include "main.h"
#include "dac.h"
#include "gpio.h"
#include "iwdg.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "public.h"
#include "dac_ctrl.h"
#include "lamp.h"
#include "fan.h"
#include "cuankou.h"
#include "flash_eeprom.h"

void SystemClock_Config(void);

/* ====================== 主函数 ====================== */

int main(void)
{
    /* ---- 第1步: HAL + 时钟 ---- */
    HAL_Init();
    SystemClock_Config();

    /* ---- 第2步: 外设初始化 ---- */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_DAC_Init();
    MX_USART1_UART_Init();
    MX_IWDG_Init();

    /* ---- 第3步: 应用层初始化 ---- */
    init();

    /* ---- 第4步: PVD 掉电检测 ---- */
    PWR_PVDTypeDef sConfigPVD = {0};
    sConfigPVD.PVDLevel = PWR_PVDLEVEL_4;
    sConfigPVD.Mode = PWR_PVD_MODE_IT_RISING_FALLING;
    HAL_PWR_ConfigPVD(&sConfigPVD);
    HAL_PWR_EnablePVD();
    __HAL_PWR_PVD_EXTI_ENABLE_IT();

    /* ---- 第5步: 主循环 ---- */
    while (1)
    {
        /* 喂狗 */
        IWDG->KR = 0xAAAA;

        /* 更新系统滴答 */
        g_lamp.tick = HAL_GetTick();

        /* ========== 5ms: DAC 渐变 + 编码器扫描 + 应答队列 ========== */
        static uint32_t last_fade = 0;
        if ((g_lamp.tick - last_fade) >= g_config.tick_fade_ms)
        {
            last_fade = g_lamp.tick;
            DAC_fade_tick();
            encoder_scan();
            rsp_flush();
        }

        /* ========== 100ms: 风扇控制 ========== */
        static uint32_t last_fan = 0;
        if ((g_lamp.tick - last_fan) >= g_config.tick_fan_ms)
        {
            last_fan = g_lamp.tick;

            /* 根据温度分段控制风扇转速 */
            int16_t t = g_lamp.temperature;
            if      (t < FAN_T1) fan_set(0);       // 温度 < T1: 风扇停转
            else if (t < FAN_T2) fan_set(64);      // T1~T2: 低速 25%
            else if (t < FAN_T3) fan_set(128);     // T2~T3: 中速 50%
            else if (t < FAN_T4) fan_set(192);     // T3~T4: 高速 75%
            else                 fan_set(254);     // > T4: 全速 100%
        }

        /* ========== 1000ms: 温度读取 + 报警 + 定时上报 + 关机保存 ========== */
        static uint32_t last_alarm = 0;
        if ((g_lamp.tick - last_alarm) >= g_config.tick_alarm_ms)
        {
            last_alarm = g_lamp.tick;

            /* 读取温度传感器 */
            temperature_read();

            /* 每秒通过串口发送一次状态报告 */
//         Status_Report_Periodic();

            /* 关机渐变完成后保存到 Flash (仅关机时, 避免频繁擦写) */
            if (g_lamp.save_brightness && g_lamp.lamp_state == 0 && dac_fade_is_idle())
            {
                flash_eeprom_save_brightness();
            }
        }

        /* 空闲时进入休眠省电 (由任意中断唤醒) */
        __WFI();
    }
}

/* ====================== 系统时钟配置 ====================== */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();
}

/* ====================== 错误处理 ====================== */

void Error_Handler(void)
{
    __disable_irq();    // 关闭全局中断
    while (1)
    {
        ledoff();       // 紧急关灯
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
