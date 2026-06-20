/**
 * @file    public.c
 * @brief   公共功能函数 — 配置、DAC控制、温度、风扇、初始化
 * @note    特性:
 *            - 非线性亮度映射 (对数曲线, 护眼)
 *            - 软启动 / 软关断渐变 (20步 × 5ms = 100ms)
 *            - 色温混合 (2700K~6500K, 白光+黄光混合)
 *-            - PVD 掉电检测 → 紧急 Flash 保存
 */
#include "public.h"
#include "dac_ctrl.h"
#include "lamp.h"
#include "fan.h"
#include "ds18b20.h"
#include "flash_eeprom.h"
#include <string.h>
#include <math.h>

/* ====================== 项目配置 (只读) ====================== */
const Project_Config_T g_config = {
    .current_max_mA      = 900,
    .current_min_mA      = 60,
    .current_off_mA      = 0,

    .dac_vref_mV         = 3300,
    .dac_resolution      = 4095,

    .fade_steps          = 20,

    .temp_alarm_trigger  = 900,
    .temp_alarm_reset    = 800,

    .fan_pwm_arr         = 254,
    .fan_temp_t1         = 100,
    .fan_temp_t2         = 150,
    .fan_temp_t3         = 200,
    .fan_temp_t4         = 250,

    .version_major       = 2,
    .version_minor       = 0,

    .tick_fade_ms        = 5,
    .tick_fan_ms         = 100,
    .tick_alarm_ms       = 1000,
};

/* ====================== 设备运行时状态 ====================== */
Lamp_Device_T g_lamp;
/* ====================== DAC 控制 ====================== */
/* DAC 内部启动标志 (只需调用 Start 一次) */
static uint8_t g_dac_started_ch1 = 0;
static uint8_t g_dac_started_ch2 = 0;

/* 
 * 目标追踪渐变系统 (伺服舵机风格)
 * 
 * 核心思想: DAC_set() 只更新目标电压 (ISR 安全, 无硬件操作)
 *          DAC_fade_tick() 每 tick 将当前电压朝目标推进固定步长
 * 
 * 优点:
 *   - 连续指令只更新目标, 不重置进度 → 无卡顿
 *   - 灯始终匀速平滑移动, 不会在"前几步"徘徊
 *   - ISR 中只修改变量, 硬件操作统一在主循环
 */
static volatile uint8_t  g_fade_active[2];          // 1 = 追踪进行中 (当前≠目标)
static uint16_t g_current_mV[2];           // 当前虚拟电压 (mV), 每 tick 更新
static volatile uint16_t g_target_mV[2];            // 目标电压 (mV), 由 DAC_set() (ISR) 更新
static volatile uint8_t  g_shutdown_fade[2];        // 1 = 关机渐变 (慢速)
static uint8_t  g_shutdown_tick_skip[2];   // 关机减速计数器 (每3 tick 推进1步)

/* 步长: 正常 5mV/tick → 1000mV/s, 关机 ~1.7mV/tick → ~333mV/s (约1.5s覆盖全范围) */
#define FADE_STEP_NORMAL_MV     5
#define FADE_STEP_SHUTDOWN_MV   1
#define SHUTDOWN_TICK_DIVIDER   3       // 关机每3 tick 推进1步
/**
 * @brief  DAC 输出指定电压 (纯整数运算, 无浮点误差)
 */
void DAC_SetVoltage_Int(uint32_t dac_channel, uint16_t voltage_mv)
{
uint32_t dac_code;
	
if (voltage_mv > g_config.dac_vref_mV)   voltage_mv = g_config.dac_vref_mV;
	
dac_code = ((uint32_t)voltage_mv * g_config.dac_resolution + g_config.dac_vref_mV / 2) / g_config.dac_vref_mV;
	
HAL_DAC_SetValue(&hdac, dac_channel, DAC_ALIGN_12B_R, (uint16_t)dac_code);
	
if (dac_channel == DAC_CHANNEL_1)
{
if (g_dac_started_ch1 == 0)
{
HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
g_dac_started_ch1 = 1;
}
}
else
{
if (g_dac_started_ch2 == 0)
{
HAL_DAC_Start(&hdac, DAC_CHANNEL_2);
g_dac_started_ch2 = 1;
}
}
}
/**
- @brief  电流(mA) → 输出电压(V) — 反比校准公式
- @note   V = 1.30122 - 0.00039671 * ImA
-     这是一个反比关系：电压越高 → 电流越小 → 灯越暗

-     验证数据：

-       ImA=255.2 → V=1.2000V

-       ImA=3.0   → V=1.3000V

-       ImA=128.4 → V=1.2503V

-     临界值：V ≥ 1.301V 时电流≈0，灯彻底关闭

*/
double mA_to_V(double ImA)
{
return 1.30122 - 0.00039671 * ImA;
}
/**
- @brief  电压(V) → 电流(mA) — 反函数（备用）
- @note   ImA = (1.30122 - U) / 0.00039671
-     简化: ImA ≈ -2520.7 * U + 3280.0

*/
double V_to_I(double U)
{
return -2.5207 * U + 3.2800;
}
/**
- @brief  亮度(1~99) → 电流(mA) — 对数曲线映射
- @note   人眼对亮度的感知遵循韦伯-费希纳定律（对数关系）
-     亮度   0 → 关灯 (电流为0，输出电压 = mA_to_V(current_off_mA))

-     亮度   1 → I_min (最小可亮电流)

-     亮度  50 → I_max * 0.1 (10% 电流感知约 50% 亮度)

-     亮度  99 → I_max (全功率)

- 
-     公式: I = I_min * exp( (b-1)/98 * ln(I_max/I_min) )

-     此曲线提供平滑、护眼的调光效果

- 
-     适配不同设备: 只需修改 g_config 中的 current_max_mA / current_min_mA /

-     current_off_mA 即可适配不同的 LED 驱动板

- @param  brightness 亮度值 0~99 (0=关灯, 1=微光, 99=全亮)
- @return 对应电流 mA (0 表示关灯)
*/
uint16_t brightness_to_current_mA(uint8_t brightness)
{
if (brightness == 0) return g_config.current_min_mA;      // 亮度0 → 最低亮度电流
if (brightness >= 99) return g_config.current_max_mA;     // 全亮 → 最大电流
// 对数插值: 亮度值 [1, 98] 映射到 [I_min, I_max]
double ratio = (double)(brightness - 1) / 98.0;
double log_range = log((double)g_config.current_max_mA / (double)g_config.current_min_mA);
double current = (double)g_config.current_min_mA * exp(ratio * log_range);
// 钳位保护，确保不超出配置范围
if (current > (double)g_config.current_max_mA)
current = (double)g_config.current_max_mA;
if (current < (double)g_config.current_min_mA)
current = (double)g_config.current_min_mA;
return (uint16_t)(current + 0.5);   // 四舍五入取整
}
/**
- @brief  亮度值 → DAC 目标电压 (mV)
- @note   转换链: brightness → 电流(mA) → 电压(V) → 电压(mV)
-     反比关系: 亮度越高 → 电流越大 → 电压越低

-     brightness=0 (关灯): 通过 mA_to_V(current_off_mA) 动态计算关灯电压

-                          例如 current_off_mA=3 → 关灯电压≈1300mV

-     brightness=99 (全亮): 通过 mA_to_V(current_max_mA) 计算

-                           例如 current_max_mA=150 → 全亮电压≈1242mV

- 
-     适配不同设备: 修改 current_off_mA 即可改变关灯电压阈值

-     注意：绝不能输出 0V，因为 0V 对应最大电流 ~3280mA，会烧灯！

- @param  brightness 亮度值 0~99
- @return DAC 输出电压 mV
*/
static uint16_t brightness_to_voltage_mV(uint8_t brightness)
{
// 亮度0 → 最低亮度 (微光模式)，使用 current_min_mA 计算电压
// 亮度0不等于关灯，关灯由 lamp_state=0 控制
if (brightness == 0)
{
double min_V = mA_to_V((double)g_config.current_min_mA);
return (uint16_t)(min_V * 1000.0 + 0.5);   // 四舍五入
}
// 亮度 → 电流 → 电压 → mV
uint16_t current_mA = brightness_to_current_mA(brightness);
double voltage_V = mA_to_V((double)current_mA);
return (uint16_t)(voltage_V * 1000.0 + 0.5);
}

/**
 * @brief  获取关机电压 (mV) — 用于彻底关灯
 * @note   通过 mA_to_V(current_off_mA) 动态计算关灯电压
 *         绝不输出 0V（反比关系中 0V = 最大电流 ~3280mA，会烧灯！）
 */
static uint16_t off_voltage_mV(void)
{
    double off_V = mA_to_V((double)g_config.current_off_mA);
    return (uint16_t)(off_V * 1000.0 + 0.5);
}
/**
- @brief  亮度 → 电流(mA) → 电压(V) → DAC 码值（完整转换链）
- @note   写入 DAC 前必须通过此函数计算，确保经过电流校准
-     转换步骤:

-       1. brightness → current_mA (对数曲线)

-       2. current_mA  → voltage_V  (反比校准公式)

-       3. voltage_V   → DAC code   (12位量化)

- @param  brightness 亮度值 0~100
- @return 12位 DAC 码值 (0~4095)
*/
uint16_t brightness_to_dac_code(uint8_t brightness)
{
    uint16_t mV = brightness_to_voltage_mV(brightness);
    return (uint16_t)(((uint32_t)mV * g_config.dac_resolution
             + g_config.dac_vref_mV / 2) / g_config.dac_vref_mV);
}

/**
 * @brief  读取 DAC 硬件寄存器当前输出码值
 * @param  channel_idx 0=ch9/DAC_CH1, 1=ch10/DAC_CH2
 * @return 12位 DAC 码值 (0~4095)
 */
uint16_t dac_get_current_code(uint8_t channel_idx)
{
    if (channel_idx == 0)
        return (uint16_t)(DAC->DHR12R1 & 0xFFF);
    else
        return (uint16_t)(DAC->DHR12R2 & 0xFFF);
}
/**
 * @brief  DAC 码值 → 输出电压 (mV)
 * @note   code * Vref / 4095
 * @param  code 12位 DAC 码值
 * @return 输出电压 mV
 */
uint16_t dac_code_to_mV(uint16_t code)
{
    return (uint16_t)(((uint32_t)code * g_config.dac_vref_mV) / g_config.dac_resolution);
}
/**
- @brief  立即设置 DAC 输出（无渐变）— 用于上电初始化
- @note   直接设置硬件并同步虚拟位置
- @param  channel_idx 0=ch9/DAC_CH1, 1=ch10/DAC_CH2
*/
void DAC_set_instant(uint8_t channel_idx)
{
if (channel_idx > 1) return;
uint32_t dac_ch = (channel_idx == 0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

uint16_t mV;
if (g_lamp.temp_alarm == 1 || g_lamp.lamp_state == 0)
{
mV = off_voltage_mV();
}
else
{
mV = brightness_to_voltage_mV(g_lamp.brightness[channel_idx]);
}

DAC_SetVoltage_Int(dac_ch, mV);
g_current_mV[channel_idx]      = mV;
g_target_mV[channel_idx]       = mV;
g_fade_active[channel_idx]     = 0;
g_shutdown_fade[channel_idx]   = 0;
g_shutdown_tick_skip[channel_idx] = 0;
}

/**
- @brief  设置 DAC 目标 — 仅更新目标电压, 不操作硬件 (ISR 安全)
- @note   亮度0 → 最低亮度 (微光模式)
-     关机 (lamp_state=0) → 目标设为关机电压, 慢速追踪
-     温度报警 → 立即关断 (安全优先)
- @param  channel_idx 0 = ch9/DAC_CH1(白光), 1 = ch10/DAC_CH2(黄光)
*/
void DAC_set(uint8_t channel_idx)
{
if (channel_idx > 1) return;
uint32_t dac_ch = (channel_idx == 0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

// 温度报警 → 立即关断 (安全优先, 直接写硬件)
if (g_lamp.temp_alarm == 1)
{
uint16_t off_mV = off_voltage_mV();
DAC_SetVoltage_Int(dac_ch, off_mV);
g_current_mV[channel_idx]      = off_mV;
g_target_mV[channel_idx]       = off_mV;
g_fade_active[channel_idx]     = 0;
g_shutdown_fade[channel_idx]   = 0;
g_shutdown_tick_skip[channel_idx] = 0;
return;
}

// 关机 → 目标设为关机电压, 启动慢速追踪
if (g_lamp.lamp_state == 0)
{
uint16_t off_mV = off_voltage_mV();
g_target_mV[channel_idx] = off_mV;
g_shutdown_fade[channel_idx] = 1;
g_shutdown_tick_skip[channel_idx] = 0;
g_fade_active[channel_idx] = (g_current_mV[channel_idx] != off_mV) ? 1 : 0;
return;
}

// 正常亮度 → 目标设为对应电压, 启动正常追踪
g_shutdown_fade[channel_idx]   = 0;
g_shutdown_tick_skip[channel_idx] = 0;
uint16_t target_mV = brightness_to_voltage_mV(g_lamp.brightness[channel_idx]);
g_target_mV[channel_idx] = target_mV;
g_fade_active[channel_idx] = (g_current_mV[channel_idx] != target_mV) ? 1 : 0;
}

/**
 * @brief  查询所有通道渐变是否空闲
 * @return 1=全部空闲, 0=有渐变进行中
 */
uint8_t dac_fade_is_idle(void)
{
    return (g_fade_active[0] == 0 && g_fade_active[1] == 0) ? 1 : 0;
}

/**
- @brief  渐变时钟 tick — 将当前电压朝目标推进固定步长
- @note   每 g_config.tick_fade_ms (5ms) 调用一次（主循环中）
-     正常步长: 5mV/tick → 1000mV/s, 全范围~120ms
-     关机步长: 1mV每3tick → ~67mV/s, 全范围~1.8s
-     使用线性插值在 start_mV → target_mV 之间平滑过渡
*/
void DAC_fade_tick(void)
{
for (uint8_t i = 0; i < 2; i++)
{
if (!g_fade_active[i]) continue;

uint32_t dac_ch = (i == 0) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
int16_t step;

// 关机渐变: 每 SHUTDOWN_TICK_DIVIDER 个 tick 才推进一步
if (g_shutdown_fade[i])
{
g_shutdown_tick_skip[i]++;
if (g_shutdown_tick_skip[i] < SHUTDOWN_TICK_DIVIDER) continue;
g_shutdown_tick_skip[i] = 0;
step = FADE_STEP_SHUTDOWN_MV;
}
else
{
step = FADE_STEP_NORMAL_MV;
}

// 朝目标推进
if (g_current_mV[i] < g_target_mV[i])
{
g_current_mV[i] += step;
if (g_current_mV[i] >= g_target_mV[i])
{
g_current_mV[i] = g_target_mV[i];
g_fade_active[i] = 0;
g_shutdown_fade[i] = 0;
}
}
else
{
g_current_mV[i] -= step;
if (g_current_mV[i] <= g_target_mV[i])
{
g_current_mV[i] = g_target_mV[i];
g_fade_active[i] = 0;
g_shutdown_fade[i] = 0;
}
}

DAC_SetVoltage_Int(dac_ch, g_current_mV[i]);
}
}
/* ====================== 色温混合 ====================== */
/**
 * @brief  应用色温，混合白光 (ch9) + 黄光 (ch10)
 * @param  kelvin: 色温值 K (2700 ~ 6500)
 * @note   暖光: 黄光通道主导 (2700K → 白光=10%, 黄光=100%)
 *         冷光: 白光通道主导 (6500K → 白光=100%, 黄光=10%)
 *         中性: 两者均衡 (4600K → 白光=50%, 黄光=50%)
 *
 *         总亮度（感知上）大致保持不变，
 *         同时色温平滑过渡。
 */
void color_temp_apply(uint16_t kelvin)
{
if (kelvin < 2700) kelvin = 2700;
if (kelvin > 6500) kelvin = 6500;
g_lamp.color_temp_k    = kelvin;
g_lamp.color_temp_mode = 1;

/* 线性映射:
 *   kelvin = 2700 → white_ratio=0.0, yellow_ratio=1.0
 *   kelvin = 4600 → white_ratio=0.5, yellow_ratio=0.5
 *   kelvin = 6500 → white_ratio=1.0, yellow_ratio=0.0
 */
float t = (float)(kelvin - 2700) / (float)(6500 - 2700);
uint8_t white_b  = (uint8_t)(t * 100.0f + 0.5f);
uint8_t yellow_b = (uint8_t)((1.0f - t) * 100.0f + 0.5f);

/* 确保至少有一点光 */
if (white_b < 1)  white_b  = 1;
if (yellow_b < 1) yellow_b = 1;

g_lamp.brightness[0] = white_b;
g_lamp.brightness[1] = yellow_b;

DAC_set(0);
DAC_set(1);

g_lamp.lamp_state = 1;
g_lamp.save_brightness = 1;

}
/* ====================== 关灯 ====================== */
void ledoff(void)
{
    /* 紧急关断 — 无渐变, 安全优先 */
    /* 注意：绝对不能输出 0V！反比关系中 0V = 最大电流 ~3280mA，会烧灯 */
    uint16_t off_mV = off_voltage_mV();  // 关灯电压 (≈1301mV)
    DAC_SetVoltage_Int(DAC_CHANNEL_1, off_mV);
    DAC_SetVoltage_Int(DAC_CHANNEL_2, off_mV);
    // 同步虚拟位置
    g_current_mV[0] = off_mV;
    g_current_mV[1] = off_mV;
    g_target_mV[0]  = off_mV;
    g_target_mV[1]  = off_mV;
    g_fade_active[0] = 0;
    g_fade_active[1] = 0;
    g_shutdown_fade[0] = 0;
    g_shutdown_fade[1] = 0;
    g_shutdown_tick_skip[0] = 0;
    g_shutdown_tick_skip[1] = 0;
}
/* ====================== 温度监测 ====================== */
void temperature_read(void)
{
short t_temp;

t_temp = DS18B20_Get();
g_lamp.temperature = t_temp;

/* 更新环形缓冲区 */
for (uint8_t i = 9; i > 0; i--)
{
    g_lamp.temp_recent[i] = g_lamp.temp_recent[i - 1];
}
g_lamp.temp_recent[0] = t_temp;

if (g_lamp.temp_alarm)
{
    /* 检查是否最近 10 次读数都低于复位阈值 */
    uint8_t all_cool = 1;
    for (uint8_t i = 0; i < 10; i++)
    {
        if (g_lamp.temp_recent[i] >= g_config.temp_alarm_reset)
        {
            all_cool = 0;
            break;
        }
    }
    if (all_cool)
    {
        g_lamp.temp_alarm = 0;
        DAC_set(0);     // 恢复亮度 (渐变)
        DAC_set(1);
    }
}
else
{
    /* 5 次连续超温读数 → 触发报警 */
    if ((g_lamp.temp_recent[0] > g_config.temp_alarm_trigger)
     && (g_lamp.temp_recent[1] > g_config.temp_alarm_trigger)
     && (g_lamp.temp_recent[2] > g_config.temp_alarm_trigger)
     && (g_lamp.temp_recent[3] > g_config.temp_alarm_trigger)
     && (g_lamp.temp_recent[4] > g_config.temp_alarm_trigger))
    {
        g_lamp.temp_alarm = 1;
        /* 紧急关断 - 安全优先 */
        ledoff();
    }
}

}
/* ====================== PWM 风扇控制 ====================== */
void fan_set(uint8_t fan_temp)
{
TIM2->CCR1 = fan_temp;
}
/* ====================== PVD 掉电检测 ====================== */
/**
 * @brief  PVD 掉电检测回调 — 紧急保存当前状态到 Flash
 * @note   由 HAL_PWR_PVD_IRQHandler() 调用
 *         VDD < 2.9V 时触发, 利用残余电容电量完成一次 Flash 写入
 *         Flash 擦除+写入约 40ms, 需确保板上电容足够维持
 */
void HAL_PWR_PVDCallback(void)
{
    /* 检查是否掉电沿 (VDD 下降) */
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_PVDO))
    {
        /* 紧急保存当前状态 (无条件写入, 不检查脏标记) */
        flash_eeprom_save();
    }
    else
    {
        /* 上电沿 (VDD 恢复), 清除标志即可 */
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_PVDO);
    }
}
/* ====================== 系统初始化 ====================== */
void init(void)
{
uint16_t i = 10000;

/* 调试时冻结 IWDG */
DBGMCU->CR |= GPIO_PIN_8;

/* 初始化期间喂狗 */
IWDG->KR = 0xAAAA;
HAL_Delay(100);
IWDG->KR = 0xAAAA;

/* 用安全默认值初始化运行时状态 */
memset(&g_lamp, 0, sizeof(g_lamp));
g_lamp.brightness[0]     = 50;       // 默认亮度 50
g_lamp.brightness[1]     = 50;
g_lamp.dark[0]           = 1800;
g_lamp.dark[1]           = 1800;
g_lamp.bright[0]         = 3850;
g_lamp.bright[1]         = 3850;

/* 启动温度转换 */
DS18B20_convert();

/* 从内部 Flash 读取用户参数 → 覆盖默认值 */
flash_eeprom_init();
flash_eeprom_read();

/* 初始化旋转编码器 */
encoder_init();

/* 启动 PWM 风扇先全速短暂运行，然后稳定 */
TIM2->ARR = g_config.fan_pwm_arr;
while (i--);
fan_set(255);
Fan_open;

/* 设置初始 DAC 输出 (从 0 渐变到保存的亮度) */
DAC_set_instant(0);     // ch9 先立即设置
DAC_set_instant(1);     // ch10 先立即设置

/* UART 波特率已在 HAL 层 MX_USART1_UART_Init() 中固定为 115200，无需再设 */

/* 防爆闪 */
Burst_Flash;

/* 短暂延时让电源稳定 */
HAL_Delay(50);

}





