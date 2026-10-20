/**
  ******************************************************************************
  * @file    main.c
  * @brief   bring-up 试例：点灯 + 串口打印版本号/编译时间/运行时长
  * @note    embed 技能纪律模式试例（v0.1.0）。
  *          假设：STM32F103C8T6 @72MHz（HSE 8MHz，中密度），裸机 SPL。
  *          行为：PC13 LED 每 500ms 翻转；USART1(115200-8-N-1) 上电打印横幅，
  *                之后每 1s 打印运行秒数。
  *          工程需包含：system_stm32f10x.c（SYSCLK_FREQ_72MHz）、
  *          startup_stm32f10x_md.s、stm32f10x_rcc.c、stm32f10x_gpio.c、
  *          stm32f10x_usart.c；宏 STM32F10X_MD、HSE_VALUE=8000000；
  *          Keil 勾选 Use MicroLIB。
  ******************************************************************************
  */
#include "stm32f10x.h"

#include <stdio.h>

/* Private define ------------------------------------------------------------*/
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_Pin_13
#define VERSION     "0.1.0"

/* Private variables ---------------------------------------------------------*/
static volatile uint32_t ms_ticks = 0;            /* SysTick 与主循环共享：volatile */

/* Private function prototypes -----------------------------------------------*/
static void delay_init(void);
static void led_init(void);
static void usart1_init(void);

/* USER CODE BEGIN 0 */
uint32_t millis(void) { return ms_ticks; }
/* USER CODE END 0 */

/**
  * @brief  主函数
  */
int main(void)
{
    delay_init();
    led_init();
    usart1_init();

    /* USER CODE BEGIN 2 */
    printf("\r\n== blink_uart v" VERSION ", build " __DATE__ " " __TIME__ " ==\r\n");
    printf("MCU: STM32F103C8T6 @72MHz, HSE=8MHz\r\n");

    uint32_t blink_t0 = millis();
    uint32_t log_t0   = millis();
    uint32_t uptime   = 0;
    int      led_on   = 0;
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        if (millis() - blink_t0 >= 500) {         /* 非阻塞 500ms 翻转 LED */
            blink_t0 = millis();
            led_on = !led_on;
            GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)led_on);   /* 走 BSRR，原子 */
        }
        if (millis() - log_t0 >= 1000) {          /* 每 1s 上报运行时长 */
            log_t0 = millis();
            printf("uptime %lu s\r\n", (unsigned long)++uptime);
        }
        /* USER CODE END WHILE */
    }
}

/**
  * @brief  SysTick 1ms 节拍（覆盖启动文件弱定义；全文件唯一）
  */
void SysTick_Handler(void)
{
    ms_ticks++;
}

static void delay_init(void)
{
    /* SystemCoreClock 由 system_stm32f10x.c 按 SYSCLK_FREQ_72MHz 计算 = 72_000_000 */
    if (SysTick_Config(SystemCoreClock / 1000)) {
        while (1) { ; }                           /* Reload 超 24bit 时失败：死循环可见 */
    }
}

/* 时钟使能永远在最前 ---------------------------------------------------------*/
static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);              /* 复位后先灭（板载低电平亮） */
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;                 /* TX: 复用推挽 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_10;                /* RX: 浮空输入 */
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

/* USER CODE BEGIN printf（Keil + MicroLIB；GCC 工程改用 _write） */
int fputc(int ch, FILE *f)
{
    (void)f;
    uint32_t t = 20000;                           /* 超时保护：约 >2 个字节时间，足够 TXE 置位 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
        if (--t == 0) {
            return ch;                            /* 超时丢字符，不卡死主流程 */
        }
    }
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}
/* USER CODE END printf */
