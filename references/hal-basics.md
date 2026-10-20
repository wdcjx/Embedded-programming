# HAL 基础（CubeMX + Keil，锚定 F103C8T6）

> 本技能 HAL 轨（v0.5 起）。覆盖：CubeMX 工作流纪律、句柄模型、SPL→HAL 对照、回调中断范式、F1 特有坑。
> HAL+FreeRTOS 完整集成（时基迁移细则/内核配置模板）尚未沉淀，用户要 RTOS 组合时按 §8 铁律执行并如实说明边界。

## 1. 芯片与时钟锚定（与 SPL 轨同一块板）

- STM32F103C8T6：72MHz / 64K Flash / 20K RAM，中密度。
- HSE 8MHz → PREDIV /1 → PLL ×9 → SYSCLK 72MHz；AHB /1；**APB1 /2 = 36MHz**（定时器 ×2 = 72MHz）；APB2 /1 = 72MHz；ADC 预分频 /6 = 12MHz（≤14MHz 上限）。
- CubeMX 时钟树按此配置；手工工程核对 `SystemClock_Config()` 同参数。

## 2. CubeMX 工程要点（每次新建核对）

- Chip: STM32F103C8Tx；Toolchain 选 **MDK-ARM**。
- SYS：Debug 选 **Serial Wire**（否则 PA13/14 被占，PA15/PB3/PB4 无法释放）；Timebase Source 默认 SysTick——**接 FreeRTOS 必须改 TIM**（§8 铁律一）。
- Project Manager：勾选 **Generate peripheral initialization as a pair of .c/.h files**（模块化，外设各占 mx_xxx.c/.h）。
- 外设配置后引脚仍按 SPL 轨同一份引脚分配表纪律（冲突查重、JTAG 释放、AFIO）。
- HSE 外部晶振：RCC → HSE = Crystal/Ceramic Resonator，否则 PLL 无从起振。
- **改造既有 CubeMX 工程先读 `.ioc`**：纯文本，芯片（`Mcu.CPN`）、引脚（`PA11.Signal=GPIO_Input`）、时钟树（`RCC.*Freq_Value`）、固件版本（`ProjectManager.FirmwarePackage`）、工具链（`TargetToolchain`）、NVIC 分组全在里面——比读代码更快更准。
- 实测锚点：CubeMX 6.18.1 + STM32Cube FW_F1 **V1.8.7** + MDK-ARM V5.32（F103C8T6 GPIO+SWD 最小工程回归通过）。

## 3. 生成代码结构与 USER CODE 区纪律（最高频丢代码坑）

```
Core/Src/  main.c  stm32f1xx_it.c  stm32f1xx_hal_msp.c  mx_gpio.c mx_usart.c …
Core/Inc/  main.h  …
Drivers/STM32F1xx_HAL_Driver/        # HAL 全家桶（不要手工改）
MDK-ARM/                             # Keil 工程与启动文件
```

- **一切业务代码只写在 `/* USER CODE BEGIN x */ … /* USER CODE END x */` 区间内**。区间外的改动在下次重新生成代码时被静默删除。
- 回调函数（`HAL_GPIO_EXTI_Callback`、`HAL_UART_RxCpltCallback`…）必须写在 USER CODE 区（约定放 main.c 或自建模块，并加注释说明所在区）。
- 自建业务模块（bsp_xxx.c/.h）放 Core/Src、Core/Inc，手工加入 MDK 工程 Group；CubeMX 重生成不会删自建文件，但 .h 引用要自己加。
- 源码编码：CubeMX 生成文件为英文无编码问题；自写中文注释文件按坑清单 17 处理（GBK/UTF-8 与 Keil 设置一致）。
- **启动文件 CubeMX 自动放好**：F103C8/CB（中密度）用 `MDK-ARM/startup_stm32f103xb.s`——命名与 SPL 轨 `startup_stm32f10x_md.s` 不同，不必自备。
- **MspInit 集中在 `stm32f1xx_hal_msp.c`**：`HAL_MspInit()`（全局：AFIO/PWR 时钟 + `__HAL_AFIO_REMAP_SWJ_NOJTAG()`——CubeMX F1 默认 Serial Wire 且自动释放 JTAG 引脚，无需 SPL 式手动 remap）与各外设 `HAL_xxx_MspInit` 都生成在这；往 CubeMX 工程补 MspInit 必须写进该文件，**另写一份同名函数 → 链接期重复定义**。模板 main_hal.c 把 MspInit 内联在 main.c 是"手写起点"的简化，对照 CubeMX 工程时注意归属。
- 时基位置：`stm32f1xx_it.c` 的 `SysTick_Handler()` 调 `HAL_IncTick()`——默认时基在这；迁 TIM 后（§8）由 CubeMX 重排，勿手工恢复。
- USER CODE 区还有**函数内细分区**（如 `MX_GPIO_Init_1` / `MX_GPIO_Init_2`），要在函数体中间插代码用它们。

## 4. SPL → HAL 心智模型对照

| SPL（旧轨） | HAL（本轨） |
|---|---|
| 直接函数 + 配置结构体 | **句柄**（`huart1`/`hspi1`）+ `HAL_xxx_Init` + 状态机 + 回调 |
| `RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)` | `__HAL_RCC_GPIOA_CLK_ENABLE()`（宏） |
| `USART_SendData` / RXNE 轮询 | `HAL_UART_Transmit/Receive`(阻塞)、`_IT`(中断+回调)、`_DMA` |
| `TIM_ClearITPendingBit` 手动清标志 | `HAL_TIM_IRQHandler` 内部清标志 → 调回调 |
| `TIM_CtrlPWMOutputs(TIM1, ENABLE)` 手动 MOE | `HAL_TIM_PWM_Start` 内部已处理，不需要 |
| `ADC_ResetCalibration/StartCalibration` | `HAL_ADCEx_Calibration_Start(&hadc1)`（**F1 必须手动加**，CubeMX 不生成，见 §7） |
| 软件 I2C / 事件轮询 | `HAL_I2C_Mem_Read/Write(&hi2c1, addr, reg, …)`；地址为 7 位左移 1 位后的 8 位形式不含 R/W；扫描用 `HAL_I2C_IsDeviceReady` |
| `spi_rw` + 手动 CS | `HAL_SPI_TransmitReceive` + **CS 仍需手动 GPIO** |
| `delay_ms()`（SysTick 自管） | `HAL_Delay(ms)`（基于时基）；微秒无内置——用 DWT->CYCCNT 方案 |

- **HAL 返回值必须检查**：`HAL_StatusTypeDef`（OK/ERROR/BUSY/TIMEOUT）；TIMEOUT 是正常失败路径，要有失败分支，不许静默。
- printf 重定向（Keil MicroLIB）：`fputc` 内用 `HAL_UART_Transmit(&huart1, (uint8_t*)&ch, 1, 超时)`；GCC 工具链用 `_write`。

## 5. 中断与回调范式（SPL ISR 纪律的 HAL 等价物）

- 向量函数（`USART1_IRQHandler` 等）在 `stm32f1xx_it.c`，CubeMX 生成，内部只调 `HAL_UART_IRQHandler(&huart1)`。
- `HAL_xxx_IRQHandler` 清标志后调用用户回调 `HAL_UART_RxCpltCallback(UART_HandleTypeDef*)` —— **回调运行在中断上下文**：只取数据、置标志，禁止 `HAL_Delay`/阻塞（铁律二，违反可能死锁，见 §7）。
- EXTI 回调统一入口 `HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`，按 Pin 分支。
- volatile 规则、CM3 原子性、PRIMASK 临界区与 SPL 轨完全相同，见 isr-and-concurrency-rules.md。
- 优先级：HAL 默认分组 **NVIC_PRIORITYGROUP_4**（4 位抢占，与 SPL 轨 Group_2 习惯不同）；CubeMX 已在 `HAL_Init` 后配置一次，全工程不再改。

## 6. validator 的 HAL 规则（validate_generated.py 已支持）

- HAL 工程识别：`stm32f1xx_hal.h` include 或 HAL API 特征。
- 时钟检查同时接受 `__HAL_RCC_xxx_CLK_ENABLE` 宏（无任何时钟使能才报 VIOLATION，宏抽象容忍同 SPL 轨）。
- **ISR 与 Callback 内 `HAL_Delay` → VIOLATION**（铁律二）。
- RTOS + HAL 同时检出：提示确认时基已迁 TIM。

## 7. F1 HAL 起步坑（本轨已知）

1. **F1 ADC 校准不自动**：`MX_ADC1_Init()` 之后、首次采样前必须手动 `HAL_ADCEx_Calibration_Start(&hadc1)`（CubeMX 不生成）。症状：数值漂移，同 SPL 轨坑 2。
2. **回调里 HAL_Delay 死锁**：时基为 SysTick 时，在优先级 ≥ SysTick 的中断回调里调 `HAL_Delay`，tick 永不递增 → 死等。铁律二的原因。
3. **USER CODE 区外写代码被重生成删除**（§3，最高频丢代码原因）。
4. **HSE_VALUE**：`stm32f1xx_hal_conf.h` 默认 8000000，晶振不符则波特率/延时会全错（同 SPL 轨坑 1）。
5. AFIO/重映射：CubeMX 自动写 `__HAL_RCC_AFIO_CLK_ENABLE` 与重映射；手工补代码时别漏（同 SPL 轨坑 3/4）。
6. TIM 业务与 HAL 时基抢定时器：时基改 TIM 后（如 TIM1/TIM4），该定时器归系统，业务 PWM/TIM 不得占用（CubeMX 选 TimeBase 时会标黄提示）。

## 8. HAL + FreeRTOS 铁律（完整集成文档待沉淀）

1. **时基必须迁 TIM**：CubeMX 启用 FreeRTOS 中间件时，TimeBase Source 改为非 SysTick 的 TIM；`HAL_InitTick` 归属该 TIM，SysTick 给内核（CubeMX 的 FreeRTOS 集成会自动处理 PendSV/SVC）。
2. **任务里禁 `HAL_Delay`**：任务延时用 `osDelay`/`vTaskDelay`；`HAL_Delay` 是忙等，只允许在调度器启动前或驱动初始化内部短延时。
3. 中断/回调优先级数字 ≥5（configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY）才能调 FreeRTOS API 的 FromISR 版本——同 SPL 轨红线。
4. 其余（内核文件、heap_4、向量接管、FromISR 范式）参照 freertos-spl-integration.md 的通用部分，CubeMX 会代劳文件放置；生成后仍须跑 validate。
