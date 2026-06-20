/**
 * @file    flash_eeprom.h
 * @brief   STM32F1 内部 Flash 模拟 EEPROM（磨损均衡）
 * @note    STM32F103RCT6: 256KB Flash, 2KB/页, 10K 擦写寿命
 *          使用末 2 页 (126, 127) 交替存储, 可靠写入 >120 万次
 *          100 年寿命: 每天写 35 次以内安全
 */

#ifndef __FLASH_EEPROM_H
#define __FLASH_EEPROM_H

#include "public.h"

#ifdef __cplusplus
extern "C" {
#endif

void flash_eeprom_init(void);
void flash_eeprom_read(void);
void flash_eeprom_save(void);
void flash_eeprom_save_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_EEPROM_H */
