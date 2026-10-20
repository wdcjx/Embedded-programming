/**
  ******************************************************************************
  * @file    main_rtos_spl.c
  * @brief   STM32F103C8T6 标准库(SPL) + FreeRTOS(ARM_CM3) 任务工程骨架
  * @note    embed 技能模板（RTOS 轨）。与裸机 templates/main.c 的差异：
  *          1) 删除 delay_init/millis/SysTick_Handler —— SysTick 已归 FreeRTOS；
  *          2) NVIC 分组必须是 Group_4；
  *          3) ISR 只用 FromISR API + portEND_SWITCHING_ISR；
  *          4) 任务延时只用 vTaskDelay。
  *          用户侧需加入内核文件清单见 references/freertos-spl-integration.md §1。
  *          配套：FreeRTOSConfig.h（同目录）。printf 用法同裸机（fputc + MicroLIB）。
  ******************************************************************************
  */
#include "stm32f10x.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/* Private define ------------------------------------------------------------*/
#define LED_PORT    GPIOC
#define LED_PIN     GPIO_Pin_13

/* 任务栈单位是字（4 字节）；用 printf 的任务给大一点 */
#define LED_TASK_STACK      128
#define KEY_TASK_STACK      128

/* Private variables ---------------------------------------------------------*/
static volatile TaskHandle_t s_key_task = NULL;   /* ISR 使用的句柄：volatile + 初始化后只读 */

/* Private function prototypes -----------------------------------------------*/
static void nvic_init_group(void);
static void led_init(void);
static void key_init(void);
static void usart1_init(void);
static void led_task(void *argument);
static void key_task(void *argument);

/* ---------------- FreeRTOS 钩子（FreeRTOSConfig.h 已开启，必须实现） ---------------- */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) { ; }                               /* 堆耗尽：查 configTOTAL_HEAP_SIZE */
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    while (1) { ; }                               /* 栈溢出：加大该任务栈 */
}

void vAssertCalled(const char *pcFile, int pcLine)
{
    (void)pcFile;
    (void)pcLine;
    taskDISABLE_INTERRUPTS();
    while (1) { ; }
}

/* 注意：本文件不得定义 SVC_Handler / PendSV_Handler / SysTick_Handler，
 *      三者已由 FreeRTOSConfig.h 宏接管。 */

/* USER CODE BEGIN 0 */
int fputc(int ch, FILE *f)                        /* Keil + MicroLIB；GCC 改 _write */
{
    (void)f;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        ;                                         /* WARNING(validator)：短等待，由任务频率约束，可接受 */
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}
/* USER CODE END 0 */

/**
  * @brief  主函数：只做初始化 + 建任务 + 启动调度器，没有主循环业务
  */
int main(void)
{
    nvic_init_group();                            /* FreeRTOS：全抢占分组，必须最先配一次 */
    led_init();
    key_init();
    usart1_init();

    printf("\r\n== SPL+FreeRTOS v0.1, build " __DATE__ " " __TIME__ " ==\r\n");

    if (xTaskCreate(led_task, "led", LED_TASK_STACK, NULL, 2, NULL) != pdPASS) {
        printf("create led task failed\r\n");     /* 堆不够：查 configTOTAL_HEAP_SIZE */
    }
    if (xTaskCreate(key_task, "key", KEY_TASK_STACK, NULL, 2, &s_key_task) != pdPASS) {
        printf("create key task failed\r\n");
    }
    vTaskStartScheduler();

    while (1) { ; }                               /* 正常到不了这里；到不了调度器说明堆耗尽 */
}

/* ---------------- 任务 ---------------- */
static void led_task(void *argument)              /* 500ms 翻转 LED */
{
    (void)argument;
    int on = 0;
    for (;; ) {
        on = !on;
        GPIO_WriteBit(LED_PORT, LED_PIN, (BitAction)on);   /* WriteBit 走 BSRR，原子 */
        vTaskDelay(pdMS_TO_TICKS(500));                    /* 任务延时唯一正确姿势 */
    }
}

static void key_task(void *argument)              /* 等按键 ISR 的任务通知 */
{
    (void)argument;
    for (;; ) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);     /* 阻塞等 ISR 唤醒 */
        printf("key event @tick=%u\r\n", (unsigned)xTaskGetTickCount());
        /* 消抖/业务：任务里做，不要搬进中断 */
    }
}

/* ---------------- 中断（FromISR 标准范式） ---------------- */
void EXTI0_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
        vTaskNotifyGiveFromISR(s_key_task, &woken);        /* 只能用 FromISR API */
        portEND_SWITCHING_ISR(woken);                      /* 立即切换到被唤醒任务 */
    }
}

/* ---------------- SPL 初始化（与裸机相同写法） ---------------- */
static void nvic_init_group(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);       /* FreeRTOS 要求：全 4 位抢占 */
}

static void led_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_PORT, &gpio);
    GPIO_SetBits(LED_PORT, LED_PIN);
}

static void key_init(void)
{
    GPIO_InitTypeDef gpio;
    EXTI_InitTypeDef  exti;
    NVIC_InitTypeDef  nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    gpio.GPIO_Pin  = GPIO_Pin_0;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);

    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    exti.EXTI_Line    = EXTI_Line0;
    exti.EXTI_Mode    = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = EXTI_Trigger_Falling;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    /* 抢占优先级必须 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)，
     * 否则 ISR 里不允许调 FromISR API（会触发 configASSERT）。 */
    nvic.NVIC_IRQChannel                   = EXTI0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 5;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin   = GPIO_Pin_10;
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
