# STM32 标准外设库（StdPeriph / SPL）API 速查

> 以 F1（stm32f10x_stdperiph_lib）为准。工程宏 `STM32F10X_MD` 等决定寄存器映射，启动文件按密度选择。

## 0. 时钟使能

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC
                     | RCC_APB2Periph_USART1 | RCC_APB2Periph_ADC1
                     | RCC_APB2Periph_SPI1  | RCC_APB2Periph_AFIO, ENABLE);
RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3
                     | RCC_APB1Periph_USART2 | RCC_APB1Periph_I2C1, ENABLE);
RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
```

注意：`GPIOx`、`USART1`、`ADC1`、`SPI1` 在 APB2；`TIM2~7`、`USART2/3`、`I2C1/2`、`SPI2` 在 APB1。用 EXTI 或重映射必须开 `RCC_APB2Periph_AFIO`。

## 1. GPIO

```c
GPIO_InitTypeDef gpio;
gpio.GPIO_Pin   = GPIO_Pin_13;
gpio.GPIO_Mode  = GPIO_Mode_Out_PP;      /* 见下表 */
gpio.GPIO_Speed = GPIO_Speed_50MHz;      /* 仅输出/复用模式需要 */
GPIO_Init(GPIOC, &gpio);

GPIO_SetBits(GPIOC, GPIO_Pin_13);        /* 输出 1 */
GPIO_ResetBits(GPIOC, GPIO_Pin_13);      /* 输出 0 */
GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);
GPIOC->ODR ^= GPIO_Pin_13;               /* 翻转（SPL 无 Toggle 函数） */

uint8_t  in  = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0);
uint8_t  out = GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13);
uint16_t all = GPIO_ReadInputData(GPIOA);
```

| GPIO_Mode | 用途 |
|---|---|
| `GPIO_Mode_Out_PP` / `GPIO_Mode_Out_OD` | 推挽/开漏输出 |
| `GPIO_Mode_IN_FLOATING` / `GPIO_Mode_IPU` / `GPIO_Mode_IPD` | 浮空/上拉/下拉输入 |
| `GPIO_Mode_AIN` | ADC/DAC 模拟 |
| `GPIO_Mode_AF_PP` / `GPIO_Mode_AF_OD` | 复用推挽（串口TX/SPI/PWM）/复用开漏（I2C） |

F1 JTAG 释放（需要 PA15/PB3/PB4 作 GPIO 时）：

```c
RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);  /* 保留 SWD */
```

## 2. 外部中断 EXTI

```c
/* 输入 GPIO 用 IPU/IPD/IN_FLOATING，然后： */
RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);

EXTI_InitTypeDef exti;
exti.EXTI_Line    = EXTI_Line0;
exti.EXTI_Mode    = EXTI_Mode_Interrupt;
exti.EXTI_Trigger = EXTI_Trigger_Falling;   /* Rising / Falling / Rising_Falling */
exti.EXTI_LineCmd = ENABLE;
EXTI_Init(&exti);

NVIC_InitTypeDef nvic;
nvic.NVIC_IRQChannel                   = EXTI0_IRQn;
nvic.NVIC_IRQChannelPreemptionPriority = 2;
nvic.NVIC_IRQChannelSubPriority        = 0;
nvic.NVIC_IRQChannelCmd                = ENABLE;
NVIC_Init(&nvic);
```

服务函数（固定名，全工程一份）：

```c
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        /* 只置标志，不耗时 */
    }
}
```

## 3. USART

```c
USART_InitTypeDef usart;
usart.USART_BaudRate            = 115200;
usart.USART_WordLength          = USART_WordLength_8b;
usart.USART_StopBits            = USART_StopBits_1;
usart.USART_Parity              = USART_Parity_No;
usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
USART_Init(USART1, &usart);
USART_Cmd(USART1, ENABLE);
```

收发（阻塞）：

```c
void uart_send_byte(uint8_t b)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);  /* 等发送区空 */
    USART_SendData(USART1, b);
}
void uart_send_str(const char *s) { while (*s) uart_send_byte((uint8_t)*s++); }
/* 发完一帧后如需确认线路空闲： while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET); */

uint8_t uart_recv_byte(void)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET); /* 等收到数据 */
    return (uint8_t)USART_ReceiveData(USART1);   /* 读 DR 自动清 RXNE */
}
```

中断接收：

```c
USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);   /* 可选：空闲帧判定 */
/* NVIC: USART1_IRQn */

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(USART1);  /* 读 DR 即清标志 */
        /* 存入环形缓冲，只置标志 */
    }
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET) {
        (void)USART1->SR;                                /* 先读 SR */
        (void)USART1->DR;                                /* 再读 DR，清 IDLE */
        frame_ready = 1;
    }
}
```

printf 重定向（Keil 需勾选 Use MicroLIB）：

```c
#include <stdio.h>
int fputc(int ch, FILE *f)
{
    (void)f;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/* GCC（CubeIDE / arm-none-eabi）改用： */
int _write(int file, char *ptr, int len)
{
    (void)file;
    for (int i = 0; i < len; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, (uint8_t)ptr[i]);
    }
    return len;
}
```

## 4. TIM — 定时中断 / PWM

```c
TIM_TimeBaseInitTypeDef tim;
tim.TIM_Period        = 1000 - 1;          /* ARR */
tim.TIM_Prescaler     = 72 - 1;            /* 72MHz/72 = 1MHz 计数 */
tim.TIM_ClockDivision = TIM_CKD_DIV1;
tim.TIM_CounterMode   = TIM_CounterMode_Up;
TIM_TimeBaseInit(TIM2, &tim);
TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
TIM_Cmd(TIM2, ENABLE);
/* NVIC: TIM2_IRQn */

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        /* 只置标志 */
    }
}
```

PWM（TIM3_CH1 → PA6）：

```c
GPIO 配置：PA6 用 GPIO_Mode_AF_PP。

TIM_OCInitTypeDef oc;
oc.TIM_OCMode      = TIM_OCMode_PWM1;
oc.TIM_OutputState = TIM_OutputState_Enable;
oc.TIM_Pulse       = 500;                  /* 占空比 = Pulse/(ARR+1) */
oc.TIM_OCPolarity  = TIM_OCPolarity_High;
TIM_OC1Init(TIM3, &oc);
TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
TIM_Cmd(TIM3, ENABLE);

TIM_SetCompare1(TIM3, duty);                /* 运行中改占空比 */
uint16_t cnt = TIM_GetCounter(TIM3);
```

通道对应：`TIM_OC1Init/TIM_SetCompare1 ↔ CH1`，以此类推。
**高级定时器 TIM1/TIM8** 输出 PWM 还必须：`TIM_CtrlPWMOutputs(TIM1, ENABLE);`（置 MOE），且对应 OCxInit 带 `TIM_OutputNState`（互补通道时）。

## 5. ADC

```c
ADC_InitTypeDef adc;
adc.ADC_Mode               = ADC_Mode_Independent;
adc.ADC_ScanConvMode       = DISABLE;        /* 单通道 */
adc.ADC_ContinuousConvMode = DISABLE;        /* 单次软件触发 */
adc.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
adc.ADC_DataAlign          = ADC_DataAlign_Right;
adc.ADC_NbrOfChannel       = 1;
ADC_Init(ADC1, &adc);

/* 通道+采样时间在此配置（PA0 = ADC_Channel_0） */
ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);
ADC_Cmd(ADC1, ENABLE);

/* F1 使用前必须校准一次 */
ADC_ResetCalibration(ADC1);
while (ADC_GetResetCalibrationStatus(ADC1) == SET);
ADC_StartCalibration(ADC1);
while (ADC_GetCalibrationStatus(ADC1) == SET);
```

单次转换读取：

```c
uint16_t adc_read(void)
{
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    uint32_t t = 100000;
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
        if (--t == 0) return 0;               /* 超时保护 */
    return ADC_GetConversionValue(ADC1);      /* 读后 EOC 自动清 */
}
/* 电压 = raw * 3.3 / 4095 */
```

## 6. I2C（主机，事件轮询式）

```c
I2C_InitTypeDef i2c;
i2c.I2C_Mode                = I2C_Mode_I2C;
i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
i2c.I2C_OwnAddress1         = 0x33;           /* 主机自身地址，避免与从机冲突即可 */
i2c.I2C_Ack                 = I2C_Ack_Enable;
i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
i2c.I2C_ClockSpeed          = 100000;         /* 100kHz，最快 400kHz */
I2C_Init(I2C1, &i2c);
I2C_Cmd(I2C1, ENABLE);
```

带超时的事件等待宏（推荐先定义）：

```c
static int i2c_wait(uint32_t event)
{
    uint32_t t = 20000;                        /* 简易超时 */
    while (I2C_CheckEvent(I2C1, event) != SUCCESS)
        if (--t == 0) return -1;
    return 0;
}

/* 读寄存器型器件（大部分传感器/OLED）：addr 为 7 位地址 */
int i2c_mem_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    I2C_GenerateSTART(I2C1, ENABLE);
    if (i2c_wait(I2C_EVENT_MASTER_MODE_SELECT)) return -1;
    I2C_Send7bitAddress(I2C1, (uint8_t)(addr << 1), I2C_Direction_Transmitter);
    if (i2c_wait(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) return -1;
    I2C_SendData(I2C1, reg);
    if (i2c_wait(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) return -1;

    I2C_GenerateSTART(I2C1, ENABLE);           /* 重复起始 */
    if (i2c_wait(I2C_EVENT_MASTER_MODE_SELECT)) return -1;
    I2C_Send7bitAddress(I2C1, (uint8_t)(addr << 1), I2C_Direction_Receiver);
    if (i2c_wait(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) return -1;

    while (len) {
        if (len == 1) I2C_AcknowledgeConfig(I2C1, DISABLE);  /* 最后一字节 NAK */
        if (i2c_wait(I2C_EVENT_MASTER_BYTE_RECEIVED)) return -1;
        *buf++ = (uint8_t)I2C_ReceiveData(I2C1);
        len--;
    }
    I2C_GenerateSTOP(I2C1, ENABLE);
    I2C_AcknowledgeConfig(I2C1, ENABLE);
    return 0;
}

/* 写：START → 地址(W) → reg → 数据… → STOP，事件套路同上去掉重复起始段 */
```

总线扫描：对 1~126 逐个执行「START + 发地址」，`i2c_wait(...TRANSMITTER_MODE_SELECTED...)` 成功即存在该地址；失败需 `I2C_GenerateSTOP` 复位总线再继续。F1 硬件 I2C 历史上有 bug，对时序敏感的传感器可改软件模拟 I2C（GPIO 开漏 + 延时）。

## 7. SPI（主机）

```c
SPI_InitTypeDef spi;
spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
spi.SPI_Mode              = SPI_Mode_Master;
spi.SPI_DataSize          = SPI_DataSize_8b;
spi.SPI_CPOL              = SPI_CPOL_Low;          /* 模式0：CPOL=0 CPHA=1Edge */
spi.SPI_CPHA              = SPI_CPHA_1Edge;
spi.SPI_NSS               = SPI_NSS_Soft;          /* 片选用 GPIO 手动控制 */
spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16;  /* 72/16 = 4.5MHz */
spi.SPI_FirstBit          = SPI_FirstBit_MSB;
SPI_Init(SPI1, &spi);
SPI_Cmd(SPI1, ENABLE);

/* 全双工读写的最小单元：发一字节同时收一字节 */
uint8_t spi_rw(uint8_t d)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, d);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}
/* 只读外设发 0xFF；只写忽略返回值。CS 片选在收发前后拉低/拉高。 */
```

## 8. 延时与节拍

标准库没有 `HAL_Delay`，模板用 SysTick 1ms 中断实现（见 templates/main.c）：

```c
delay_init();                 /* main 里先调用：SysTick_Config(SystemCoreClock/1000) */
uint32_t t0 = millis();
if (millis() - t0 >= 500) { t0 = millis(); /* 每 500ms */ }
delay_ms(10);                 /* 阻塞延时，仅初始化/明确允许时用 */
```

## 9. 独立看门狗 IWDG

```c
IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
IWDG_SetPrescaler(IWDG_Prescaler_32);     /* LSI 40kHz/32 = 1.25kHz */
IWDG_SetReload(1249);                     /* ≈1s 超时 */
IWDG_ReloadCounter();
IWDG_Enable();
/* 主循环稳定路径上喂狗： */ IWDG_ReloadCounter();
```

不要把喂狗放在中断里（主程序卡死时中断仍会喂狗，失去看护意义）。

## 10. 密度宏 / 启动文件 / 工程文件清单

| Flash 容量 | 宏 | 启动文件 | 例 |
|---|---|---|---|
| ≤ 32K | `STM32F10X_LD` | startup_stm32f10x_ld.s | C6T6 |
| ≤ 128K | `STM32F10X_MD` | startup_stm32f10x_md.s | **C8T6** |
| ≤ 512K | `STM32F10X_HD` | startup_stm32f10x_hd.s | ZET6 / VET6 |

工程必备：`system_stm32f10x.c`（含 72MHz 时钟配置，宏 `SYSCLK_FREQ_72MHz`）+ 上面启动文件 + 按需添加的标准库驱动：
`stm32f10x_rcc.c`、`stm32f10x_gpio.c`、`misc.c`（NVIC 用）常备；`stm32f10x_usart.c`、`stm32f10x_tim.c`、`stm32f10x_adc.c`、`stm32f10x_i2c.c`、`stm32f10x_spi.c`、`stm32f10x_exti.c`、`stm32f10x_dma.c`、`stm32f10x_iwdg.c`… 按外设启用，并在 `stm32f10x_conf.h` 中放开对应 `#include`。

共性提醒：

- `stm32f10x.h` 中 `HSE_VALUE` 必须与板载晶振一致（默认 8000000），否则波特率/延时全错；
- 中断服务函数名固定（`USART1_IRQHandler`…），拼错不报错但永远不进中断；
- 标准库已停止维护，仅老系列可用——新系列需求见 SKILL.md「重要边界」。


## 11. DAC（HD/XL 才有，C8T6 无）

- 时钟：`RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE)`；PA4/PA5 配 `GPIO_Mode_AIN`
- 核心：`DAC_Init(DAC_Channel_1, &D)` → `DAC_Cmd(DAC_Channel_1, ENABLE)` → `DAC_SetChannel1Data(DAC_Align_12b_R, 0~4095)`
- 常用无触发直写（`DAC_Trigger_None`）；完整配方与波形发生架构见 peripheral-recipes.md §12/§13

## 12. FSMC（HD/XL 才有，TFT-LCD 并口）

- 时钟：`RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC, ENABLE)`——**AHB 总线**，不是 APB2；GPIO 走 APB2 + AFIO
- 流程：数据/控制线全部 `GPIO_Mode_AF_PP` → `FSMC_NORSRAMInitTypeDef`（MemoryType_NOR / DataWidth_16b / AccessMode_B / 时序 AddressSetup=DataSetup=0x0F）→ `FSMC_NORSRAMInit(&f)` + `FSMC_NORSRAMCmd(Bank, ENABLE)`
- Bank 基址：NE1~NE4 = `0x60000000 / 0x64000000 / 0x68000000 / 0x6C000000`；RS 接 A10 → 16 位宽度下用地址 bit11 区分命令/数据
- 引脚全表/初始化序列/R-B 交换/坑：见 fsmc-lcd-ili9341.md
