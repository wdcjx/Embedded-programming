/*
 * bad_sample_rtos.c — SPL + FreeRTOS 组合的故意违规自测样本
 * 用途：python ../scripts/validate_generated.py bad_sample_rtos.c
 * 期望：多条 VIOLATION（ISR 用任务级 API、ISR 内 vTaskDelay、
 *       非 volatile 句柄被 ISR 使用、RTOS 工程调 SysTick_Config），退出码 1。
 * 注意：本文件仅供检查脚本自测，不可编译、不可上板。
 */

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

QueueHandle_t uart_queue;        /* 违规：ISR 使用但无 volatile */

static void blink_task(void *argument)
{
    (void)argument;
    for (;; ) {
        GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);
        vTaskDelay(pdMS_TO_TICKS(500));              /* 任务里用 vTaskDelay：正确 */
    }
}

void USART1_IRQHandler(void)                         /* ISR */
{
    BaseType_t woken = pdFALSE;
    uint8_t d;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        d = (uint8_t)USART_ReceiveData(USART1);
        xQueueSend(uart_queue, &d, 0);               /* 违规：必须 xQueueSendFromISR */
        vTaskDelay(1);                               /* 违规：ISR 内延时（无 FromISR 版本） */
        portEND_SWITCHING_ISR(woken);                /* woken 从未被置位：逻辑违规，静态不查 */
    }
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);  /* 违规(警告)：RTOS 必须 Group_4 */
    SysTick_Config(SystemCoreClock / 1000);          /* 违规：SysTick 已被 FreeRTOS 接管 */

    uart_queue = xQueueCreate(16, sizeof(uint8_t));
    xTaskCreate(blink_task, "led", 128, NULL, 2, NULL);
    vTaskStartScheduler();

    while (1) { ; }
}
