/**
  ******************************************************************************
  * @file    main.c
  * @brief   STM32F103C8T6 标准库（StdPeriph/SPL）工程骨架（72MHz, HSE=8MHz）
  * @note    embed 技能模板：SysTick 毫秒节拍 + 非阻塞点灯 + USART1 printf。
  *
  *          工程配置要点：
  *          - 宏定义：STM32F10X_MD（C8T6 中密度），HSE_VALUE=8000000
  *          - 启动文件：startup_stm32f10x_md.s
  *          - system_stm32f10x.c：SYSCLK_FREQ_72MHz（进 main 前自动跑 SystemInit）
  *          - 需添加的标准库驱动：stm32f10x_rcc.c、stm32f10x_gpio.c、
  *            stm32f10x_usart.c、misc.c（用 NVIC 时）
  *          - Keil：勾选 Use MicroLIB（printf 重定向用 fputc）
  *
  *          注意：单独把本文件放在本目录用 VS Code 打开时，
  *          "无法打开源文件 stm32f10x.h / stdio.h"（C/C++ 1696）属预期现象——
  *          stm32f10x.h 在 SPL 库包里、stdio.h 在编译器工具链里，都不在本目录。
  *          放入含标准库的实际工程后即消除；详见 README 常见问题。
  ******************************************************************************
  */
#include "stm32f10x.h"

#include <stdio.h>

/* Private define ------------------------------------------------------------*/
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_Pin_13
#define LED_ON()    GPIO_ResetBits(LED_PORT, LED_PIN)   /* 板载 LED 低电平点亮 */
#define LED_OFF()   GPIO_SetBits(LED_PORT, LED_PIN)

/* Private variables ---------------------------------------------------------*/
static volatile uint32_t ms_ticks = 0;

/* Private function prototypes -----------------------------------------------*/
static void delay_init(void);
static void led_init(void);
static void usart1_init(void);

/* USER CODE BEGIN 0 */
uint32_t millis(void) { return ms_ticks; }

void delay_ms(uint32_t ms)
{
    uint32_t t0 = millis();
    while (millis() - t0 < ms) ;
}

void delay_us(uint32_t us)   /* 粗略微秒延时（72MHz 实测校准倍率），勿用于精确时序 */
{
    uint32_t n = us * 8;
    while (n--) { __NOP(); }
}
/* USER CODE END 0 */

/**
  * @brief  主函数
  */
int main(void)
{
    delay_init();
    /* NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  用中断时在此配置一次分组 */
    led_init();
    usart1_init();

    /* USER CODE BEGIN 2 */
    printf("\r\n== firmware v0.1, build " __DATE__ " " __TIME__ " ==\r\n");
    uint32_t blink_t0 = millis();
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        if (millis() - blink_t0 >= 500) {      /* 非阻塞 500ms 翻转 */
            blink_t0 = millis();
            LED_PORT->ODR ^= LED_PIN;
        }
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
  * @brief  SysTick 1ms 节拍（覆盖启动文件中的弱定义）
  */
void SysTick_Handler(void)
{
    ms_ticks++;
}

static void delay_init(void)
{
    /* SystemCoreClock 由 system_stm32f10x.c 按 SYSCLK_FREQ_72MHz 算好 = 72_000_000 */
    if (SysTick_Config(SystemCoreClock / 1000)) {
        while (1) ;                            /* 配置失败死循环 */
    }
}

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);           /* 复位后先灭灯 */
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;              /* TX: 复用推挽 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_10;             /* RX: 浮空输入 */
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

/* USER CODE BEGIN printf（Keil + MicroLIB；GCC 工程改用 _write，见 API 速查 §3） */
int fputc(int ch, FILE *f)
{
    (void)f;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) ;
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}
/* USER CODE END printf */

/**
  * @brief  中断服务函数集中区（示例：EXTI0；按需添加，全工程只保留一份）
  */
/* USER CODE BEGIN IRQ */
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        /* 只置标志，不在中断里做耗时操作 */
    }
}
/* USER CODE END IRQ */
