# MDK-ARM 编译 & 烧录工具链参考手册

## 1. 环境信息

| 项目 | 详情 |
|------|------|
| IDE | Keil uVision V5.24.2.0 |
| 工具链 | MDK-ARM Plus Version: 5.24.1 |
| 编译器 | Armcc.exe V5.06 update 5 (build 528) |
| 汇编器 | Armasm.exe V5.06 update 5 (build 528) |
| 链接器 | ArmLink.exe V5.06 update 5 (build 528) |
| Hex转换 | FromElf.exe V5.06 update 5 (build 528) |
| 安装路径 | `C:\Keil_v5\` |
| UV4路径 | `C:\Keil_v5\UV4\UV4.exe` |
| 编译链路径 | `C:\Keil_v5\ARM\ARMCC\Bin` |
| 调试器DLL | `STLink\ST-LINKIII-KEIL_SWO.dll V3.0.1.0` |
| 目标芯片 | STM32F103RCT6 |
| 工程文件 | `.\MDK-ARM\STM32F103RCT6.uvprojx` |
| 输出目录 | `.\MDK-ARM\STM32F103RCT6\` |

---

## 2. 命令行编译

### 2.1 全量重编译 (Rebuild All)

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -r ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0
```

| 参数 | 说明 |
|------|------|
| `-r` | 全量重编译 (Rebuild all targets) |
| `-j0` | 不显示 GUI 窗口，静默编译 |
| `-b` | 仅编译修改过的文件 (Build) |
| `-c` | 清理编译产物 |
| `-o <file>` | 输出日志到指定文件 |

### 2.2 增量编译 (Build)

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -b ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0
```

### 2.3 编译并输出日志

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -r ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0 -o ".\MDK-ARM\build_output.log"
```

---

## 3. 命令行烧录 (Flash Download)

### 3.1 烧录到目标芯片

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -f ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0
```

| 参数 | 说明 |
|------|------|
| `-f` | 下载/烧录 (Flash Download) |
| `-j0` | 不显示 GUI 窗口 |

### 3.2 一键编译+烧录

```powershell
& "C:\Keil_v5\UV4\UV4.exe" -r ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0
if ($LASTEXITCODE -eq 0) {
    & "C:\Keil_v5\UV4\UV4.exe" -f ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0
}
```

---

## 4. 编译日志查看

编译日志位于输出目录下的 HTML 文件：

```
.\MDK-ARM\STM32F103RCT6\STM32F103RCT6.build_log.htm
```

关键信息在 `<h2>Output:</h2>` 之后，重点关注：
- 每个源文件的编译结果 (warning / error)
- 链接阶段: `linking...` 之后的未定义符号错误
- 最终结果: `"STM32F103RCT6.axf" - X Error(s), Y Warning(s).`
- 固件大小: `Program Size: Code=XXXX RO-data=XXXX RW-data=XXXX ZI-data=XXXX`
- 编译耗时: `Build Time Elapsed: HH:MM:SS`

---

## 5. 编译产物

| 文件 | 说明 |
|------|------|
| `STM32F103RCT6.axf` | ARM 可执行文件 (含调试信息) |
| `STM32F103RCT6.hex` | Intel HEX 格式固件 |
| `STM32F103RCT6.build_log.htm` | 编译日志 |
| `*.o` | 各模块目标文件 |

---

## 6. 常见问题排查

### 6.1 链接错误: Undefined symbol

```
Error: L6218E: Undefined symbol <函数名> (referred from <源文件>.o).
```

**原因**: 函数声明了但未实现，或 .c 文件未加入工程。

**解决**: 在对应 .c 文件中实现函数，或将缺失的 .c 文件加入工程。

### 6.2 编译警告: nested comment is not allowed

**原因**: 在 `/* ... */` 注释内部使用了 `/**` 或 `/*`。

**解决**: 统一使用 `/* ... */` 块注释，避免嵌套。

### 6.3 编译警告: last line of file ends without a newline

**原因**: 文件末尾缺少换行符。

**解决**: 在文件最后一行末尾添加一个空行。

### 6.4 编译警告: function declared implicitly

**原因**: 函数在使用前未声明或未包含对应头文件。

**解决**: 添加函数声明到 .h 头文件，或在 .c 文件顶部添加 `static` 声明。

---

## 7. 工程文件结构参考

```
项目根目录\
├── Core\                    # STM32 HAL 库文件
│   ├── Inc\                 # 头文件
│   └── Src\                 # 源文件 (main.c, gpio.c, dac.c, tim.c, usart.c...)
├── Drivers\                 # HAL 驱动库
│   └── STM32F1xx_HAL_Driver\
├── MDK-ARM\                 # Keil 工程目录
│   ├── STM32F103RCT6.uvprojx    # 工程文件
│   └── STM32F103RCT6\           # 编译输出目录
│       ├── STM32F103RCT6.hex    # 固件
│       └── STM32F103RCT6.build_log.htm  # 编译日志
├── USER\                    # 用户应用层代码
│   ├── public.h / public.c      # 全局定义 & 初始化
│   ├── cuankou.c                # 串口通信协议
│   ├── eeprom.c                 # EEPROM 存储
│   ├── keydisp.c                # 按键 & 显示
│   ├── moni_i2c.c               # I2C 监控
│   ├── tm1650.c                 # TM1650 数码管驱动
│   └── DS18B20.c                # 温度传感器驱动
├── Skill\                   # 开发文档
│   ├── 嵌入式开发规范.md
│   └── MDK-ARM编译烧录工具链.md  (本文件)
└── history\                 # 版本记录
    └── versionChange.md
```

---

## 8. 快速参考卡片

```powershell
# 一键编译+烧录 (推荐)
.\build_and_flash.ps1

# 全量编译
& "C:\Keil_v5\UV4\UV4.exe" -r ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0

# 增量编译
& "C:\Keil_v5\UV4\UV4.exe" -b ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0

# 烧录
& "C:\Keil_v5\UV4\UV4.exe" -f ".\MDK-ARM\STM32F103RCT6.uvprojx" -j0

# 编译日志
.\MDK-ARM\STM32F103RCT6\STM32F103RCT6.build_log.htm
```