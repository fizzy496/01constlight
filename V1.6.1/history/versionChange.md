V 1. 0   2026.6.19

V 1. 1   2026.6.19
  - 修复 public.c: PVD 中断处理函数注释损坏，改为块注释 /* ... */
  - 修复 cuankou.c: 补全 3 个命令处理函数 (Cmd_QueryVersion, Cmd_QueryTemp, Cmd_SaveEEPROM)
  - 修复 cuankou.c: 补全辅助函数实现 (SendOK, SendErr, Uart_SendString, ParseDec2, ch_to_idx, apply_brightness)
  - 修复 public.c: 补全 DAC 函数实现 (dac_get_current_code, dac_code_to_mV)
  - 编译成功: 0 Error(s), 14 Warning(s), Program Size: Code=27858 RO-data=1238 RW-data=48 ZI-data=2416
  - 烧录到 STM32F103RCT6 (UV4 -f)

V 1. 2   2026.6.19
  - 新增 Skill/MDK-ARM编译烧录工具链.md: 编译、烧录命令参考手册

V 1. 3   2026.6.19
  - 修复 UART 通讯: Usart1_Printf 从 DMA TX 改为阻塞 HAL_UART_Transmit (解决 DMA TX 与中断内 SendOK 直接写 DR 的冲突)
  - 新增 Status_Report_Periodic(): 每秒上报灯光状态 (亮度/电流/DAC电压/温度)
  - main.c: 1秒定时调用 Status_Report_Periodic()
  - 编译烧录成功: 0 Error(s), 14 Warning(s)

V 1. 4   2026.6.19
  - 修复 UART 通讯根因: HAL_UART_Transmit 内部中断 TX 与 USART1_IRQHandler 的 if/else 冲突
    (RX+TEX 同时触发时只处理 RX，跳过 TXE 导致 HAL_UART_Transmit 卡死)
  - Usart1_Printf 改为直接寄存器写入 (与 Uart_SendString/SendOK 一致)
  - USART1_IRQHandler 简化为只处理 RXNE，移除 HAL_UART_IRQHandler 调用
  - 编译烧录成功: 0 Error(s)

V 1. 5   2026.6.19
  - 新增 build_and_flash.ps1: 一键编译+烧录自动化脚本 (含错误检测和日志解析)
  - 更新 MDK-ARM编译烧录工具链.md: 快速参考卡片加入脚本调用

V 1. 6   2026.6.19
  - 修复 stm32f1xx_it.c: USART1_IRQHandler 缺少闭合括号 (编译错误)
  - 重写 main.c: 统一缩进, 移除调试打印, 修复 tick 更新顺序, 英文注释
  - 移除 init() 后的 HAL_Delay(2000) + Usart1_Printf 调试代码

V 1. 7   2026.6.19
  - 修复串口无法接收命令: USART RXNE 中断源 (USART_CR1.RXNEIE) 未使能
  - 在 MX_USART1_UART_Init() 中添加 __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE)
  - NVIC 中断已开启，但外设内部 RXNE 中断未开，导致 RX 完全静默

V 1. 8   2026.6.19
  - 修复 DAC 输出不变化: DAC_fade_tick() 在 main.c 中被注释掉，渐变状态机启动后从未推进
  - main.c 主循环增加 5ms DAC_fade_tick() 调用
  - HAL_Delay(200) 改为 HAL_Delay(1)，解除 200ms 阻塞以提高渐变精度