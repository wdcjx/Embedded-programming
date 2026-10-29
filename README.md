# embed — 嵌入式（STM32）开发辅助 Skill

一个给 AI Agent 用的技能（Skill）：让 Agent 能独立完成简单 STM32 项目的代码生成
（GPIO / UART / TIM / ADC / I2C / SPI / EXTI / DMA / 看门狗 / OLED 显示屏），支持四条轨：
**裸机 SPL（默认）**、**SPL + FreeRTOS**、**裸机 HAL（CubeMX + Keil，起步）**、**HAL + FreeRTOS**，
输出带引脚分配表、接线表、工程文件清单和烧录说明的完整代码。

## 生成结果长什么样

### 真实案例：大棚环境监控节点（用户实测工程）

用本技能辅助完成的真实项目：STM32F103C8T6 采集 DHT11 温湿度、SGP30 二氧化碳、土壤湿度（YL-69）、光敏（GL5506），
三路继电器驱动降温风扇 / 加湿 / 水泵，OLED 实时显示读数，ESP8266（ESP-01S）联网上报，
四个按键 + 菜单切换手动/自动模式——**Keil 编译 0 Error 0 Warning**，上电即稳定运行：

| 硬件实物 | OLED 运行实拍（温度/湿度/土壤） | Keil 编译 0 Error 0 Warning |
|---|---|---|
| <img src="assets/images/showcase-board.png" width="270"/> | <img src="assets/images/showcase-oled.png" width="190"/> | <img src="assets/images/showcase-keil-0err.png" width="340"/> |

<details>
<summary><b>硬件原理图</b>（点击展开）</summary>

<img src="assets/images/showcase-schematic.png" width="820"/>

</details>

### 一次简单需求的交付格式（示例节选）

> **需求**：「F103C8T6 串口回显，PC13 LED 心跳，printf 调试」
>
> **交付**：
> 1. `main.c`：完整可编译——HSE 8MHz×9=72MHz 时钟配置、USART1 115200 8N1、printf 重定向（fputc）、SysTick 毫秒节拍非阻塞主循环、USER CODE 分区
> 2. 引脚分配 / 接线表：
>
>    | 信号 | 引脚 | 接到 |
>    |---|---|---|
>    | USART1_TX | PA9 | USB-TTL 模块 RXD |
>    | USART1_RX | PA10 | USB-TTL 模块 TXD |
>    | LED | PC13 | 板载 LED（低电平点亮） |
>    | SWD | PA13/PA14 | ST-Link SWDIO/SWCLK |
>
> 3. 烧录方式：ST-Link SWD 一键下载（含串口 ISP 备选步骤）
> 4. validator 报告：0 VIOLATION——时钟最先开启、ISR 无阻塞调用、硬件等待带超时、NVIC 分组统一

**两种模式**：要一段能跑的简单代码 → 快速模式（直接说需求）；
要"按老手工程纪律生成、审查、检查"的代码 → 纪律模式（默认；提到 规范/审查/检查/validate 即命中）。
两种模式共用同一套 API 文件与模板，纪律文件与检查脚本只在纪律模式强制。

## 安装

**仓库地址**：<https://github.com/wdcjx/Embedded-programming>

- **通用一键安装**（Claude Code / Codex 等主流 Agent CLI）：
  ```bash
  npx skills add wdcjx/Embedded-programming
  ```
  已实测：自动识别技能名 `embed`，完整带上 `references/`、`scripts/`（validator）、`templates/` 等子目录，并生成 `skills-lock.json` 便于团队复现。
- **DeepSeek Harness (DSH)**：侧边栏「技能库」→ 粘贴仓库链接 `https://github.com/wdcjx/Embedded-programming` 安装（自动克隆 + 热加载），
  或把整个 `embed` 文件夹放到 `~/.dsh/skills/embed`；
- **手动**：`git clone https://github.com/wdcjx/Embedded-programming.git`，把克隆出的文件夹改名为 `embed` 放到 `~/.claude/skills/`（或 `~/.dsh/skills/`）。

> 注意：安装时目录名应与 SKILL.md 中的 `name: embed` 一致。

## 触发方式

对 Agent 说：「用 embed 技能生成 STM32F103C8T6 的串口回显程序」
或「帮我写 STM32 标准库点灯 + printf，PA6 输出 PWM」——涉及 STM32 / 单片机 / 标准库 / HAL / CubeMX / FreeRTOS 的需求都会命中。
提到 CubeMX 或 HAL 时自动走 HAL 轨，其余默认 SPL 轨。

## 当前能力

默认目标 **STM32F103C8T6**（72MHz，中密度 MD；其他容量按密度宏/启动文件表适配）。超出表格范围的需求 Agent 会如实说明。

| 能力域 | 内容 | 状态 |
|---|---|---|
| **四轨代码生成** | 裸机 SPL（默认）/ SPL+FreeRTOS / 裸机 HAL（CubeMX+Keil）/ HAL+FreeRTOS；输出含引脚分配表、接线表、工程文件清单、烧录说明；SysTick 毫秒节拍非阻塞主循环 + printf 重定向 | ✅ 四轨集齐（v0.5.4） |
| **外设配方（SPL）** | GPIO / EXTI / USART（printf、RXNE+IDLE 不定长帧）/ TIM（定时中断+PWM）/ ADC（F1 校准）/ I2C 主机（扫描、MPU6050）/ SPI 主机 / IWDG / DAC（HD/XL）/ 传感器驱动小抄（DS18B20 等 14 节） | ✅ recipes §1–14 |
| **显示** | OLED 0.96"（SSD1306，I2C/SPI 双版）；FSMC 驱 TFT-LCD（ILI9341/9328：地址译码/时序/窗口绘图/波形示波器信号链）；HAL 轨 LCD 四总线（SPI/FSMC/FMC/LTDC+DMA2D） | ✅ 多工程实测 |
| **通信与报警** | GSM 模块（SIM800/900）AT 短信：注网自检序列、CMGS 完整流程、回读+超时范式（禁盲等）、报警节流三件套（锁存/冷却/重发升级）、传感器阈值报警架构（滞回） | ✅ 火灾报警工程实测（v0.6.3） |
| **机电控制** | 平衡车三环（直立 PD+速度 PI+转向，并联合成、事件驱动节拍）与四轴串级 PID（角度外环+角速度内环、时间片调度）双范式；编码器测速、H 桥双电机、NRF24L01 无线链路、失联/摔倒/低压安全链设计对照 | ✅ 双实测工程（v0.8 / v0.9.2） |
| **FreeRTOS 集成** | FreeRTOSConfig.h 模板 + 任务骨架、向量接管三宏、FromISR 优先级红线（≥5）、20K RAM 堆预算、Keil 手动集成六步 SOP（SPL 轨）；时基迁 TIM 文件地图、CMSIS V1↔V2、双堆模型（HAL 轨）；**裸机库移植范式**（GRBL→F407+FreeRTOS 实测：职责表/忙等让步点/ISR 安全化/cli-sei 策略/LVGL 集成三件套）；Kernel 10.x~11.x | ✅ 实测工程 validator 0/0 |
| **跨家族** | F103C8T6 默认；F407VET6 起步（168MHz 时钟树模板、TIM2/TIM5 32 位、F1→F4 差异对照、换轨清单、CoreScribe 移植工程）；F7/H7/G0/G4/L4 无标准库，走 HAL 轨并声明未实测校对 | F4 工程级实测（v0.6.2/v0.7），未上板 |
| **工程纪律与静态检查** | `validate_generated.py`：时钟使能顺序、volatile/ISR 纪律、ISR 阻塞调用、等待超时、NVIC 分组、向量名、HAL 回调、RTOS API 误用——VIOLATION 清零才交付；坑清单 51 条（每条症状/原因/修法）；症状速查 14 条反查表；`__HAL_LOCK`/Mutex/临界区/栈堆精算/Cache+DMA 现场可靠性知识 | ✅ 6 个自测样本持续回归 |

## 常见问题

### 模板在 VS Code 里报「无法打开源文件 stm32f10x.h / stdio.h」（C/C++ 1696）

这是预期现象，不是代码错误：

- `stm32f10x.h` 在 **STM32 标准外设库（SPL）包**里，模板单独存放时自然找不到；
- `stdio.h` 在**编译器工具链**（Keil ARMCC / arm-none-eabi-gcc）里。

模板要放进含 SPL 的实际工程使用。若想在 VS Code 中获得 IntelliSense：
下载 SPL（ST 官网或 GitHub 镜像，如 `STM32F10x_StdPeriph_Lib`），在工作区建 `.vscode/c_cpp_properties.json`：

```json
{
  "configurations": [{
    "name": "STM32",
    "compilerPath": "C:/Keil_v5/ARM/ARMCC/bin/armcc.exe",
    "defines": ["STM32F10X_MD", "HSE_VALUE=8000000", "USE_STDPERIPH_DRIVER"],
    "includePath": [
      "${workspaceFolder}/**",
      "C:/path/to/STM32F10x_StdPeriph_Lib/Libraries/CMSIS/Device/ST/STM32F10x/Include",
      "C:/path/to/STM32F10x_StdPeriph_Lib/Libraries/CMSIS/Include",
      "C:/path/to/STM32F10x_StdPeriph_Lib/Libraries/STM32F10x_StdPeriph_Driver/inc"
    ]
  }]
}
```

（用 GCC 时 `compilerPath` 指向 `arm-none-eabi-gcc`；只用 Keil 编译也可以直接无视该报错。）

## 反馈与社区

- 问题/建议：[Issues](https://github.com/wdcjx/Embedded-programming/issues)——提问前先搜
  [references/known-pitfalls.md](references/known-pitfalls.md)（51 条实测坑）与
  [references/symptom-quickref.md](references/symptom-quickref.md)（症状反查表）；
- 经验交流/求助：[Discussions](https://github.com/wdcjx/Embedded-programming/discussions)；
- 想沉淀自己的实测工程或增强 validator：规矩见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 文件结构

<details>
<summary>目录树（点击展开）</summary>

```
embed/                                # 单一技能：快速生成 + 工程纪律两种模式
├── SKILL.md                          # 入口：模式路由、工作流、硬约束、自检清单
├── README.md                         # 本文件
├── LICENSE                           # MIT
├── ROADMAP.md                        # 后续计划
├── CONTRIBUTING.md                   # 贡献指南（反馈渠道/沉淀规矩/validator 回归要求）
├── .github/ISSUE_TEMPLATE/           # Issue 模板（问题反馈/工程沉淀建议）
├── references/
│   ├── stm32-stdperiph-api.md        # 标准库 API 速查 + 密度宏/工程文件清单
│   ├── peripheral-recipes.md         # 典型外设配方（点灯/串口/PWM/ADC/I2C/SPI…）
│   ├── freertos-spl-integration.md   # SPL+FreeRTOS 集成规则（向量接管/FromISR/内存预算）
│   ├── oled-ssd1306.md               # OLED 0.96"（SSD1306）驱动知识（I2C/SPI 双版）
│   ├── hal-basics.md                 # HAL 轨：CubeMX/句柄模型/SPL→HAL 对照/F1 坑/RTOS 铁律
│   ├── hal-driver-patterns.md        # HAL 第三方驱动范式（I2C/SPI/DWT 延时/传感器读数/W5500+DW1000 移植层，HAL_Driver_Lib 基准）
│   ├── freertos-hal-integration.md   # HAL+FreeRTOS 完整集成（CubeMX 实测：时基 TIM2/三宏联动/双堆）
│   ├── grbl-port-f407.md             # 裸机强实时库→STM32+FreeRTOS 移植范式（GRBL/F407 实测）
│   ├── hal-freertos-field-guide.md   # 现场可靠性：__HAL_LOCK/Mutex/临界区/栈堆精算/Cache+DMA/低功耗
│   ├── symptom-quickref.md           # 症状速查：死机/挂死/数据乱 → 根因反查
│   ├── fsmc-lcd-ili9341.md           # FSMC 驱 TFT-LCD（ILI9341，HD/XL）：地址译码/时序/绘图/坑
│   ├── f4-family-notes.md            # F4 起步（F407VET6 实测）：168MHz 时钟树/F1→F4 差异对照/换轨清单
│   ├── gsm-at-sms.md                 # GSM 模块 AT 短信报警（SIM800/900）：注网自检/CMGS/回读+超时/节流
│   ├── quad-drone-spl.md             # 2.4G 四轴遥控/飞控双机系统（F103 SPL 实测）：串级 PID/NRF 链路/混控/安全链
│   ├── balance-car-spl.md            # 两轮平衡小车（F103 SPL 实测）：事件驱动节拍/三环并联合成/现场妥协与安全链对比
│   ├── display-storage-hal.md        # LCD 四总线+SDRAM+QSPI（HAL 实测）：选型表/DMA2D/刷新率公式/bug 清单
│   ├── spl-peripheral-checklist.md   # 外设初始化顺序清单（纪律）
│   ├── isr-and-concurrency-rules.md  # ISR/并发/volatile/临界区规则（纪律）
│   └── known-pitfalls.md             # 已知坑清单（纪律）
├── templates/
│   ├── main.c                        # 裸机 SPL 工程骨架模板
│   ├── main_rtos_spl.c               # SPL+FreeRTOS 任务工程骨架模板
│   ├── FreeRTOSConfig.h              # F103 中密度 FreeRTOS 配置模板（Kernel 10.x~11.x）
│   └── main_hal.c                    # 裸机 HAL 骨架（CubeMX 对照 + 手写起点）
├── assets/
│   ├── spl-project-template.md       # Keil 工程组织/必配项/占用估算
│   └── images/                       # 首页真实案例展示图
├── examples/
│   └── blink_uart_demo/main.c         # bring-up 试例：点灯 + 串口打印版本/编译时间/运行时长
├── scripts/
│   └── validate_generated.py         # 静态纪律检查（VIOLATION 必须清零，含 RTOS/HAL 规则）
└── tests/
    ├── bad_sample.c                  # 裸机违规自测样本
    ├── bad_sample_rtos.c             # RTOS 违规自测样本
    └── bad_sample_hal.c              # HAL 违规自测样本
```

</details>

## 许可

MIT
