/**
 * @file    flash_eeprom.c
 * @brief   内部 Flash 存储 — 亮度 + 校准数据持久化
 * @note    磨损均衡: 2 页交替写入, 每页最多擦除 10K 次
 *          页 126: 0x0803F000, 页 127: 0x0803F800
 *          记录格式: magic(4B) + counter(4B) + data(12B) = 20B
 */

#include "flash_eeprom.h"

/* ====================== Flash 页地址 (STM32F103RCT6 高密度, 2KB/页) ====================== */
#define PAGE_126_ADDR  0x0803F000
#define PAGE_127_ADDR  0x0803F800

#define FLASH_MAGIC    0xDEADBEEF

/* ====================== Flash 记录结构 ====================== */
typedef struct {
    uint32_t magic;             // 魔数 0xDEADBEEF
    uint32_t counter;           // 写入计数 (单调递增)
    uint8_t  data[12];          // 用户数据 [brightness0, brightness1, 保留, 保留, dark0_H, dark0_L, bright0_H, bright0_L, dark1_H, dark1_L, bright1_H, bright1_L]
} FlashRecord_T;

/* ====================== 局部变量 ====================== */
static uint32_t g_write_counter;   // 当前写入计数
static uint32_t g_active_page;     // 当前活跃页地址

/* ====================== 初始化 — 找到最新有效记录 ====================== */

void flash_eeprom_init(void)
{
    FlashRecord_T *rec126 = (FlashRecord_T *)PAGE_126_ADDR;
    FlashRecord_T *rec127 = (FlashRecord_T *)PAGE_127_ADDR;

    uint32_t c126 = (rec126->magic == FLASH_MAGIC) ? rec126->counter : 0;
    uint32_t c127 = (rec127->magic == FLASH_MAGIC) ? rec127->counter : 0;

    if (c126 >= c127) {
        g_active_page = PAGE_126_ADDR;
        g_write_counter = c126;
    } else {
        g_active_page = PAGE_127_ADDR;
        g_write_counter = c127;
    }
}

/* ====================== 读取 — 从活跃页读取并校验数据 ====================== */

void flash_eeprom_read(void)
{
    FlashRecord_T *rec = (FlashRecord_T *)g_active_page;

    if (rec->magic != FLASH_MAGIC)
        return;

    g_lamp.brightness[0] = rec->data[0];
    g_lamp.brightness[1] = rec->data[1];

    g_lamp.dark[0]   = ((uint16_t)rec->data[4] << 8) | rec->data[5];
    g_lamp.bright[0] = ((uint16_t)rec->data[6] << 8) | rec->data[7];
    g_lamp.dark[1]   = ((uint16_t)rec->data[8] << 8) | rec->data[9];
    g_lamp.bright[1] = ((uint16_t)rec->data[10] << 8) | rec->data[11];

    /* 亮度: 1~100, 超出范围用默认值 50 */
    if (g_lamp.brightness[0] < 1 || g_lamp.brightness[0] > 100)
        g_lamp.brightness[0] = 50;
    if (g_lamp.brightness[1] < 1 || g_lamp.brightness[1] > 100)
        g_lamp.brightness[1] = 50;

    /* 暗校准: 1~2000 */
    if (g_lamp.dark[0] < 1 || g_lamp.dark[0] > 2000)
        g_lamp.dark[0] = 1800;
    if (g_lamp.dark[1] < 1 || g_lamp.dark[1] > 2000)
        g_lamp.dark[1] = 1800;

    /* 亮校准: 2500~4095 */
    if (g_lamp.bright[0] < 2500 || g_lamp.bright[0] > 4095)
        g_lamp.bright[0] = 3850;
    if (g_lamp.bright[1] < 2500 || g_lamp.bright[1] > 4095)
        g_lamp.bright[1] = 3850;
}

/* ====================== 写入 — 擦除备用页, 交替写入 ====================== */

static void flash_write_record(uint32_t page_addr)
{
    FlashRecord_T rec;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t page_error;

    /* 构建记录 */
    rec.magic   = FLASH_MAGIC;
    rec.counter = g_write_counter;
    rec.data[0] = g_lamp.brightness[0];
    rec.data[1] = g_lamp.brightness[1];
    rec.data[2] = 0;    // 保留
    rec.data[3] = 0;    // 保留
    rec.data[4]  = (uint8_t)(g_lamp.dark[0] >> 8);
    rec.data[5]  = (uint8_t)(g_lamp.dark[0] & 0xFF);
    rec.data[6]  = (uint8_t)(g_lamp.bright[0] >> 8);
    rec.data[7]  = (uint8_t)(g_lamp.bright[0] & 0xFF);
    rec.data[8]  = (uint8_t)(g_lamp.dark[1] >> 8);
    rec.data[9]  = (uint8_t)(g_lamp.dark[1] & 0xFF);
    rec.data[10] = (uint8_t)(g_lamp.bright[1] >> 8);
    rec.data[11] = (uint8_t)(g_lamp.bright[1] & 0xFF);

    /* 擦除目标页 */
    HAL_FLASH_Unlock();
    erase_init.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = page_addr;
    erase_init.NbPages     = 1;
    HAL_FLASHEx_Erase(&erase_init, &page_error);

    /* 按字写入 (20 字节 = 5 个字) */
    uint32_t *src = (uint32_t *)&rec;
    for (uint32_t i = 0; i < sizeof(FlashRecord_T); i += 4) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          page_addr + i,
                          *(src + i / 4));
    }

    HAL_FLASH_Lock();
}

void flash_eeprom_save(void)
{
    uint32_t other_page = (g_active_page == PAGE_126_ADDR)
                          ? PAGE_127_ADDR : PAGE_126_ADDR;

    g_write_counter++;
    flash_write_record(other_page);
    g_active_page = other_page;

    /* 清除脏标记 */
    g_lamp.save_brightness = 0;
    g_lamp.save_dark[0]    = 0;
    g_lamp.save_dark[1]    = 0;
    g_lamp.save_bright[0]  = 0;
    g_lamp.save_bright[1]  = 0;
}

void flash_eeprom_save_brightness(void)
{
    if (!g_lamp.save_brightness) return;
    flash_eeprom_save();
}