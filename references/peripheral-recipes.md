# 典型外设配方（F103C8T6 + 标准库，其他密度按宏与启动文件调整）

> 每个配方都是「可直接放入 main.c 的片段 + 接线」。生成完整工程时与 templates/main.c 骨架组合。

## 1. LED 闪烁（非阻塞）

接线：PC13 → LED（Pill 板载 LED 低电平点亮）。

```c
#define LED_PORT  GPIOC
#define LED_PIN   GPIO_Pin_13

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);          /* 先灭（低电平亮） */
}

/* main 循环内（delay_init() 已调用） */
static uint32_t t0 = 0;
if (millis() - t0 >= 500) {
    t0 = millis();
    LED_PORT->ODR ^= LED_PIN;                 /* 翻转 */
}
```

## 2. 按键 EXTI 中断

接线：PA0 → 按键 → GND（内部上拉，按下为低 → 下降沿触发）。

```c
static void key_init(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef  exti;
    NVIC_InitTypeDef  nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    gpio.GPIO_Pin  = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_IPU;           /* 上拉输入 */
    GPIO_Init(GPIOA, &gpio);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    exti.EXTI_Line    = EXTI_Line0;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    nvic.NVIC_IRQChannel                   = EXTI0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 2;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

volatile uint8_t key_pressed = 0;

void EXTI0_IRQHandler(void)                   /* 全工程仅此一份 */
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        key_pressed = 1;                      /* 只置标志；消抖放主循环做 */
    }
}
```

## 3. 串口 printf + 回显（轮询版）

接线：PA9(TX) → USB转串口 RX；PA10(RX) → TX；共地。115200-8-N-1。

```c
static void usart1_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_9;             /* TX: 复用推挽 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_10;            /* RX: 浮空输入 */
    gpio.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

/* 回显（轮询，简单场景；uart_send_byte/uart_recv_byte 见 API 速查 §3） */
uint8_t ch;
ch = uart_recv_byte();
uart_send_byte(ch);
```

printf 重定向见 stm32-stdperiph-api.md §3。

## 4. 串口不定长帧接收（RXNE + IDLE 空闲中断）

```c
/* USER CODE BEGIN PV */
#define RX_BUF_SIZE 128
static uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_len  = 0;
static volatile uint8_t  rx_done = 0;
static uint16_t rx_idx = 0;
/* USER CODE END PV */

/* usart1_init() 末尾追加 */
USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
/* NVIC：USART1_IRQn，PreemptionPriority=1 */

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t d = (uint8_t)USART_ReceiveData(USART1);   /* 读 DR 清 RXNE */
        if (rx_idx < RX_BUF_SIZE) rx_buf[rx_idx++] = d;
    }
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET) {
        (void)USART1->SR;                                 /* 先读 SR 再读 DR 清 IDLE */
        (void)USART1->DR;
        rx_len  = rx_idx;
        rx_idx  = 0;
        rx_done = 1;
    }
}

/* 主循环处理 */
if (rx_done) {
    rx_done = 0;
    /* 处理 rx_buf[0..rx_len-1]，期间若可能有新字节，先关 USART1_RXNE 中断 */
}
```

## 5. PWM 呼吸灯

接线：PA6 = TIM3_CH1 → LED。

```c
static void tim3_pwm_init(void)   /* 72MHz/72=1MHz 计数，ARR=999 → 1kHz PWM */
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tim;
    TIM_OCInitTypeDef       oc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_6;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    tim.TIM_Period        = 1000 - 1;
    tim.TIM_Prescaler     = 72 - 1;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &tim);

    oc.TIM_OCMode      = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse       = 0;
    oc.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM3, &oc);
    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);

    TIM_Cmd(TIM3, ENABLE);
}

/* 主循环：呼吸 */
static uint16_t duty = 0; static int8_t dir = 1; static uint32_t t0 = 0;
if (millis() - t0 >= 2) {
    t0 = millis();
    TIM_SetCompare1(TIM3, duty);
    duty += dir * 4;
    if (duty >= 999) dir = -1;
    if (duty == 0)   dir = 1;
}
```

若用 TIM1 输出，务必加 `TIM_CtrlPWMOutputs(TIM1, ENABLE);`（MOE），否则无波形。

## 6. ADC 电压采样

接线：PA0 → 被测电压（0~3.3V，勿超 VDD）。

```c
uint16_t raw = adc_read();                    /* adc_read 见 API 速查 §5（含校准与超时） */
float volt = raw * 3.3f / 4095.0f;
printf("ADC=%u, V=%.2f\r\n", raw, volt);
```

## 7. I2C 器件读取（以 MPU6050 为例）

接线：PB6=SCL、PB7=SDA → 器件，均 4.7k 上拉（多数模块自带）。MPU6050 7 位地址 0x68（AD0=GND）。
I2C GPIO 用 `GPIO_Mode_AF_OD`（复用开漏）+ 外部上拉。

```c
/* i2c_mem_read()/i2c_mem_write() 带超时实现见 API 速查 §6 */
uint8_t id = 0;
if (i2c_mem_read(0x68, 0x75, &id, 1) == 0 && id == 0x68)
    printf("MPU6050 OK\r\n");
```

F1 硬件 I2C 时序历史上有坑，对时序敏感的器件（OLED、部分传感器）建议软件模拟 I2C，后续版本会补充配方。

## 8. SPI + 片选（以 W25Q 类 Flash 为例）

接线：PA5=SCK、PA6→改用 PA7=MOSI、PA6=MISO（SPI1 默认 PA5/6/7），CS 用 PA4 推挽输出手动控制。

```c
GPIO_ResetBits(GPIOA, GPIO_Pin_4);            /* CS 低有效 */
uint8_t id_h = spi_rw(0x90); (void)id_h;      /* JEDEC ID 指令示例 */
uint8_t id1 = spi_rw(0x00), id2 = spi_rw(0x00), id3 = spi_rw(0x00);
GPIO_SetBits(GPIOA, GPIO_Pin_4);
```

注意 SPI1 默认引脚与 TIM3_CH1(PA6) 冲突，同一工程二选一。

## 9. 独立看门狗

见 stm32-stdperiph-api.md §9。喂狗放主循环稳定路径，不要放中断里。

## 10. 烧录提示（输出给用户）

- **ST-Link / SWD**：SWDIO=PA13、SWCLK=PA14，接 ST-Link，Keil/CubeProgrammer 下载；
- **串口 ISP**：BOOT0 接 1、复位 → PA9/PA10 串口用 FlyMcu/STM32CubeProgrammer 烧录，完成后 BOOT0 回 0；
- 首次验证建议先烧「点灯 + 串口打印版本号」，确认最小系统正常再上业务代码。

## 11. 传感器/总线驱动模式小抄（开源工程审查沉淀）

来源：Health_watch 开源工程（ADXL345 / MAX30102 / OLED / DS18B20 / DS1302）审查提炼——只记模式不搬代码；接入新传感器时按此套路生成。

### 软件 I2C（位敲）通用骨架
- 引脚任意 GPIO：SCL 固定输出；SDA 在「推挽输出 ↔ 上拉输入」间切换（宏封装 `I2C_SDA_OUT / I2C_SDA_IN`）。
- 核心五函数：`iic_start / iic_stop / iic_wait_ack / iic_send_byte / iic_read_byte`，时序用 us 级延时（别用 ms）。
- 同一工程允许多套软件 I2C（不同引脚各一套 .c/.h），函数名前缀区分（`iic_` / `myiic_`）避免重复定义；每套的引脚与 RCC 时钟逐一核对。
- 从器件驱动（ADXL345/MAX30102 等）只调这五个函数，不复用硬件 I2C 外设。

### DS18B20 单总线
- µs 级时序：复位拉低 480µs → 释放等 presence 应答；写时隙 60µs；读时隙拉低 ~2µs 后 15µs 内采样。
- 读时序期间必须关中断（PRIMASK 短临界区），否则中断拉长时隙导致读位错乱。
- 12 位转换最长 750ms：两次温度读取间隔 ≥750ms（与坑 24 DHT11 ≥1s 同类约束）。

### DS1302 三线（CE + IO + SCLK）
- 时间寄存器 BCD 编码存取；写寄存器前先清 WP 写保护位；支持突发模式一次读写整组时间。
- IO 方向随读/写切换（同软件 I2C 方向切换套路）。

### OLED 0.96"（SSD1306）硬件 I2C1
- PB6/PB7 复用开漏 AF_OD（I2C1 默认脚，无需重映射）；`I2C_Init` 7bit 地址 / 100k~400k。
- 事件轮询 `while(!I2C_CheckEvent(...))` 必须加超时（validator 会抓无超时忙等）。
- 初始化命令序列照驱动手册（0xAE 关显示 → … → 0xAF 开显示）；显存用全屏缓冲一次性刷新。

### 接入第三方传感器驱动（.c/.h）检查点
编码探测（GBK/UTF-8）→ 引脚/时钟清单提取 → 软件延时时基核对（是否依赖特定 delay 实现）→ 忙等加超时 → ISR 共享变量 volatile → 跑 validator 申报残留。


## 12. DAC 输出（波形发生基础）

仅 HD/XL 大容量有 DAC（**C8T6 没有**）。PA4=CH1、PA5=CH2。

```c
static void dac_init(void)
{
    GPIO_InitTypeDef G; DAC_InitTypeDef D;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
    G.GPIO_Pin = GPIO_Pin_4; G.GPIO_Mode = GPIO_Mode_AIN;   /* 模拟输入=高阻，别配成推挽 */
    GPIO_Init(GPIOA, &G);
    D.DAC_Trigger = DAC_Trigger_None;                       /* 无触发：写寄存器立即生效 */
    D.DAC_WaveGeneration = DAC_WaveGeneration_None;         /* 不用硬件噪声/三角波发生器 */
    D.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    D.DAC_OutputBuffer = DAC_OutputBuffer_Enable;           /* 带载必开；负载极轻可关提升摆率 */
    DAC_Init(DAC_Channel_1, &D);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_SetChannel1Data(DAC_Align_12b_R, 0);
}

/* 输出电压 v(0~3.3V)： */
DAC_SetChannel1Data(DAC_Align_12b_R, (uint16_t)(v / 3.3f * 4095.0f));
```

要点：无触发模式写即生效——CPU 按节拍连续写数据寄存器就得到任意波形（相位用 `fmodf(t, period)` 算，见 §13）；DAC 输出脚与 ADC 输入脚用线直连即可同芯片回环自测，是波形实验的标准接线。


## 13. 定时器节拍波形发生器 + 示波器信号链（实验九实测架构）

「ISR 算波形 → DAC 输出 → ADC 回采 → 环形缓冲降采样 → 主循环显示」五段分工：

1. **节拍**：TIM2 1ms 更新中断（PSC=71, ARR=999 @72MHz），NVIC 抢占 1。
2. **ISR 产数据**：`fmodf(t, per)` 算相位 → 波形函数得瞬时值 → 限幅 → DAC 输出 → 软件启动 ADC 读回 → 按时间窗平均后写环形缓冲（240 点，写满转滚动），到刷新点置 `g_rw` 标志。
3. **主循环刷屏**：判 `g_rw` 才动 LCD——擦旧（Fill 黑）→ 画网格（隔 4 点描点）→ DrawLine 连新波形 → 信息栏局部刷新。**ISR 绝不碰 LCD**（屏刷新几十 ms，会拖死 1ms 节拍）。
4. **向量转发**：it.c 里真向量只转发 `void TIM2_IRQHandler(void){ TIM2_IRQHandler_Callback(); }`，业务体在 main.c。回调命名用 `_Callback` 后缀，别占 `xxHandler`（见 isr-and-concurrency-rules.md §6）。
5. **状态机**：STOP/RUN/PAUSE；**改参数只在 STOP 态集中做**——RUN 中改多个 volatile float 组合参数，ISR 会采样到混合值（波形跳变）。
6. **按键**：主循环 10ms 扫描 + 连续 8 次同值判稳定（80ms），只改状态不碰屏。

ISR 预算参考：M3 软浮点每运算约 50~100 周期，1ms@72MHz 预算 72000 周期，一个 `fmodf` + 若干乘除约千级周期，余量充足；但 ADC 忙等 EOC（约 4µs）无超时仍应加护栏（检查器会 WARN）。

## 14. 传感器阈值报警架构（GSM 火灾报警沉淀）

采集（轮询/定时）→ 换算（极性决定比较方向）→ 阈值判断（**滞回**：越上限报警、回落到 阈值-滞回带 才恢复，防抖动反复）→ 声光本地报警（保底，任何情况不断）→ 远程通知（锁存只发一条+冷却+超时重发，失败不影响本地）。阈值/号码/冷却时间全宏集中；多传感器 	emp>T_t || smog>S_t || flame>F_t 任一触发，报警时短信正文带触发源与数值。

## 15. 参数掉电保存（内部 FLASH「SRAM 影子 + 整页保存」模式，江协 15-1 实测）

**架构**：运行时只读写 SRAM 影子数组；显式调用 `Store_Save()` 时整页擦除+重写。最简单可靠的掉电保存（无追加结构、无掉电窗口——代价是每次保存耗 1 次擦写寿命，10K 次额定，需按保存频率限频）。

```c
#define STORE_START_ADDRESS  0x0800FC00   // 参数页：C8T6(20K) 末页；HD(2K/页)选末页 0x0807F800 等
#define STORE_COUNT          512           // 半字数，1KB 页 = 512 半字

uint16_t Store_Data[STORE_COUNT];           // SRAM 影子，[0] 固定为标志位

void Store_Init(void)
{
    if (MyFLASH_ReadHalfWord(STORE_START_ADDRESS) != 0xA5A5)   // 首次使用（空白读 0xFFFF）
    {
        MyFLASH_ErasePage(STORE_START_ADDRESS);
        MyFLASH_ProgramHalfWord(STORE_START_ADDRESS, 0xA5A5);  // 魔数区分「从未初始化」与「初始化后清零」
        for (uint16_t i = 1; i < STORE_COUNT; i++)
            MyFLASH_ProgramHalfWord(STORE_START_ADDRESS + i*2, 0x0000);
    }
    for (uint16_t i = 0; i < STORE_COUNT; i++)                  // 整页载回 SRAM
        Store_Data[i] = MyFLASH_ReadHalfWord(STORE_START_ADDRESS + i*2);
}

void Store_Save(void)
{
    MyFLASH_ErasePage(STORE_START_ADDRESS);                     // 整页擦
    for (uint16_t i = 0; i < STORE_COUNT; i++)                  // 整页写（含标志位）
        MyFLASH_ProgramHalfWord(STORE_START_ADDRESS + i*2, Store_Data[i]);
}
```

要点：

1. **0xA5A5 首用魔数**：擦除态是 0xFFFF，任意值都可能随机命中；专用魔数可靠区分三种状态（空白/已初始化/已清零——清零后魔数保留，数据为 0）。
2. **IROM 必须给参数页让路**：Keil Target→IROM1 长度从 0x10000（64K）缩到参数页起点（C8T6 末页方案=0x4C00）。链接器不知道参数页存在，不缩的话代码长过 19KB 会静默链进参数页、烧录覆盖参数，**无任何警告**（江协原工程就没缩，代码 ~5KB 时侥幸无害——坑 67）。
3. **保存触发限频**：按键触发/关机前/定时（如 1 分钟一次且数据有变）三选一，不要在循环里高频 Save——每次 = 1 页擦写，10K 次寿命。
4. **多页参数或更严寿命需求** → 换「追加式磨损均衡」（顺序写记录、写满才擦一页，见 heater-temp-control-spl.md storage.c 实例与坑 57 掉电窗口讨论）；本模式适合 ≤1~2 页、保存次数可控的参数组。
5. 校验升级：数据段加简单和/CRC 半字，Init 时校验失败回默认值（本教学版未加，产品建议加）。

