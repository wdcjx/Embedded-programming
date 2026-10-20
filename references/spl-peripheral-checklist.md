# SPL 外设初始化顺序清单

> 生成任何外设代码前，先对照本清单核对顺序。顺序错误是"代码看着对、板上不工作"的第一大来源。

## 0. 通用铁律（每个外设都适用）

1. **时钟最先**：`RCC_APBxPeriphClockCmd(..., ENABLE)` / `RCC_AHBPeriphClockCmd(..., ENABLE)`；任何配置寄存器的写入都必须在时钟开启之后。
2. **GPIO 先于外设**：复用脚按用途配 `AF_PP` / `AF_OD`（+外部上拉）/ `AIN`，在外设 Init 之前完成。
3. **NVIC**：优先级分组 `NVIC_PriorityGroupConfig` 全工程只配一次、最先配；使能中断前先清挂起位（防止"一使能就进中断"）；再 `NVIC_Init`。
4. **外设收尾顺序**：`XXX_Init` → 需要中断则 `XXX_ITConfig` → **最后** `XXX_Cmd(ENABLE)`。
5. 特殊顺序外设（ADC 校准、IWDG 解锁）见对应小节，不可调换。

## 1. GPIO

```
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOx) → 填 GPIO_InitTypeDef → GPIO_Init
```
强制项：输出必须显式给 `GPIO_Speed`；输入模式不需要 Speed；复用功能脚的模式必须是 `AF_PP`/`AF_OD`。

## 2. EXTI（外部中断）

```
GPIO 时钟 + RCC_APB2Periph_AFIO → GPIO 输入配置 → GPIO_EXTILineConfig → EXTI_Init → 清挂起(EXTI_ClearITPendingBit) → NVIC_Init
```
强制项：**AFIO 时钟必须开**；NVIC 使能前清一次挂起位；服务函数名 `EXTIn_IRQHandler`，函数内第一件事判断并清除挂起位。

## 3. USART

```
GPIO + USART 时钟 → TX=AF_PP / RX=IN_FLOATING → USART_Init → (USART_ITConfig) → USART_Cmd(ENABLE)
```
强制项：`HSE_VALUE` 必须与实际晶振一致，否则波特率错；开 RXNE 中断时考虑 ORE 溢出的清除；收发共享的缓冲区 volatile。

## 4. TIM 定时中断

```
TIM 时钟 → TIM_TimeBaseInit → TIM_ClearITPendingBit(先清一次) → TIM_ITConfig(TIM_IT_Update) → NVIC_Init → TIM_Cmd(ENABLE)
```
强制项：APB1 分频≠1 时定时器时钟 = APB×2（F103 常用 72MHz）；`TIM_Period/Prescaler` 都是"实际值-1"。

## 5. TIM PWM

```
TIM 时钟 → GPIO AF_PP → TIM_TimeBaseInit → TIM_OCxInit → TIM_OCxPreloadConfig → TIM_Cmd(ENABLE) → (TIM_SetComparex 调占空比)
```
强制项：**TIM1/TIM8 必须加 `TIM_CtrlPWMOutputs(TIMx, ENABLE)`（MOE）**，否则无输出；SPI1 默认脚与 TIM3 CH1/CH2（PA6/PA7）冲突，二选一。

## 6. ADC（F1 必须校准）

```
GPIO(AIN) + ADC1 时钟 → ADC_Init → ADC_Cmd(ENABLE) → ADC_ResetCalibration → 等复位完成 → ADC_StartCalibration → 等校准完成 → ADC_RegularChannelConfig → 之后才可转换
```
强制项：校准流程顺序不可调换、两个等待都要执行；单次转换用 `ADC_SoftwareStartConvCmd` + 轮询 `ADC_FLAG_EOC`（带超时）。

## 7. I2C

```
GPIO(AF_OD, 外部上拉) + I2C 时钟 → I2C_Init → I2C_Cmd(ENABLE)
```
强制项：主机 `I2C_OwnAddress1` 避开从机地址；所有事件等待（`I2C_CheckEvent`）带超时；F1 硬件 I2C 出现 BUSY 卡死时按 known-pitfalls 处理。

## 8. SPI

```
GPIO(SCK/MOSI=AF_PP, MISO=IN_FLOATING, CS=推挽输出) + SPI 时钟 → SPI_Init → SPI_Cmd(ENABLE)
```
强制项：CPOL/CPHA 必须按从机手册定（模式 0~3）；NSS 用 `SPI_NSS_Soft` + GPIO 手动片选；全双工读写以 `TXE→Send→RXNE→Receive` 为最小单元。

## 9. DMA（以 USART1_TX = DMA1 通道 4 为例）

```
RCC_AHBPeriph_DMA1 (+USART 时钟) → DMA_DeInit → DMA_Init(外设地址/内存地址/方向/数量) → USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE) → DMA_Cmd(通道, ENABLE)
```
强制项：DMA 缓冲区必须是全局/静态内存；每次重发前重写 `DMA_SetCurrDataCounter` 并先 `DMA_Cmd(DISABLE)`；与 CPU 共享的缓冲区 volatile。

## 10. SysTick（节拍）

```
SysTick_Config(SystemCoreClock/1000) → 检查返回值（非 0 表示 Reload 超过 24bit，失败） → 实现 SysTick_Handler
```
强制项：Handler 内只做 `ms_ticks++`；变量 volatile。
