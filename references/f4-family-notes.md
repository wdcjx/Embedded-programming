# F4 家族起步笔记（以 F407VET6 为基准）

> 来源：text3 实测工程（D:\STM32CubeMX\text3，F407VET6 LQFP100，CubeMX 生成 + MDK-ARM）。
> 状态：**工程文件级实测**（时钟树/引脚/生成结构与 validator 回归核对），未上板、未编译验证。
> HAL 轨核心知识（USER CODE 纪律/MspInit 集中/回调=中断上下文/时基机制）与 F1 完全通用，见 hal-basics.md。

## 1. 时钟树 168MHz 满频模板（实测 .ioc/main.c 核对）

F4 的 PLL 是**三级分频**模型（F1 是固定 ×9 倍频，模型完全不同）：

```
HSE 8MHz → /PLLM=8 → 1MHz(VCO 输入，必须 1~2MHz) → ×PLLN=336 → 336MHz(VCO 输出)
   → /PLLP=2 → SYSCLK 168MHz
   → /PLLQ=4 → 48MHz（USB/SDIO 用；本工程 Q=4 实得 84MHz 未接 USB，凑 48 需 Q=7）
```

- 总线：AHB /1=168MHz；APB1 /4=42MHz（定时器自动 ×2=84MHz）；APB2 /2=84MHz（定时器=168MHz）
- Flash 等待：**FLASH_LATENCY_5**（168MHz=5WS；F1 72MHz 只要 2WS——忘记改会跑飞/取指错）
- 电源：`__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1)` + `__HAL_RCC_PWR_CLK_ENABLE()`——F4 特有的调节器量程步骤，F1 没有
- 检查锚：168 = 8/8×336/2，VCO 输入 1MHz、输出 336MHz（手册限 192~432）

## 2. F1 → F4 差异对照（生成代码时的换轨清单）

| 项 | F1（103） | F4（407） |
|---|---|---|
| 时钟模型 | HSE×9 固定 PLL | M/N/P/Q 三级分频，168MHz 满频 |
| Flash 等待 | LATENCY_2 | LATENCY_5（按频率表） |
| 电源量程 | 无 | PWR Scale1 必须（PWR 时钟先开） |
| TIM2/TIM5 | 16 位 | **32 位计数器**（ARR=0xFFFFFFFF 合法，PSC 分频做长周期） |
| APB1 频率 | 36MHz | 42MHz（定时器 84） |
| 串口家族 | USART1~3 + UART4/5 | 同结构 + USART6；USART1 在 APB2，其余 APB1 |
| 头文件/库前缀 | stm32f10x_* | stm32f4xx_*；启动文件 startup_stm32f407xx.s |
| 常用 HAL 库 | FW_F1 | FW_F4（版本区间沉淀，以实际包为准） |
| FPU | 无 | 有（Cortex-M4F；Keil 勾 Use FPU、工程宏 __FPU_PRESENT=1 由 device 头带） |
| GPIO 速度档 | 2/10/50MHz | LOW/MEDIUM/HIGH/VERY_HIGH 四档语义不同 |

不变的：Core/Src|Inc + msp.c + it.c 的 CubeMX 文件布局、USER CODE 区纪律、回调=中断上下文、HAL_Init/时基 SysTick（RTOS 时迁 TIM 的规则同 hal-basics.md §8）、NVIC Group_4、`__HAL_RCC_xxx_CLK_ENABLE` 宏形状。

## 3. 实测工程快照（text3，F407VET6）

- 引脚：UART4 **PA0=TX / PA1=RX** 115200（注意 UART4/5 在 APB1，不是 USART1 的 APB2）；TIM2_CH1 OC 输出（MspPostInit 配脚）；输入 PE0/PE4/PE5/PD3、输出 PD7（默认低）；SWD PA13/14
- TIM2 初始配置：PSC=0、ARR=0xFFFFFFFF（32 位自由计数）+ OC1 TIMING 模式——**骨架占位**，实际用前必须改 PSC/ARR/Pulse（168MHz 直驱 32 位溢出 ~25.5s）
- 输入脚 CubeMX 默认 `GPIO_NOPULL`——按键必须外部上/下拉或改 IPD/IPU，浮空读数会抖
- validator：main.c / stm32f4xx_hal_msp.c / stm32f4xx_it.c 全部 0 违规 0 警告——F4 文件对检查器零误报

## 4. F4 工程沉淀待办

- [ ] 上板实测（LED/按键/UART4 printf 回环）后补运行时坑
- [ ] TIM PWM 完整配方（PSC/ARR 计算实例：84MHz APB1 定时器 ×N 分频到目标频率）
- [ ] UART4 printf 重定向实例（fputc→HAL_UART_Transmit，与 F1 同）
- [ ] F4 + FreeRTOS（时基 TIM、CMSIS_V2 常见于 F4 工程）
- [ ] M4 FPU 编译选项核对（Keil 目标选项 FPU 与宏一致性）
