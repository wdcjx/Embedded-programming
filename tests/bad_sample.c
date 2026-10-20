/*
 * bad_sample.c — 故意包含多种违规的自测样本
 * 用途：python ../scripts/validate_generated.py bad_sample.c
 * 期望：多条 VIOLATION（非 volatile 共享变量、错误向量名、ISR 阻塞、
 *       动态内存、缺时钟使能、缺 NVIC 分组），退出码 1。
 * 注意：本文件仅供检查脚本自测，不可编译、不可上板。
 */

#include "stm32f10x.h"

int rx_byte;                 /* 违规：ISR 共享但无 volatile */
uint8_t frame[32];           /* 违规：同样被 ISR 使用但无 volatile */

void USART1_IRQHandle(void)  /* 违规：向量名拼错（应为 USART1_IRQHandler） */
{
    rx_byte = (uint8_t)USART_ReceiveData(USART1);   /* 违规：非 volatile 共享 */
    delay_ms(2);                                    /* 违规：ISR 内阻塞延时 */
    printf("%d\r\n", rx_byte);                      /* 违规：ISR 内 printf */
}

void EXTI1_IRQHandler(void)  /* 合法向量名 */
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line1);
        frame[0] = (uint8_t)rx_byte;                /* 违规：frame/rx_byte 无 volatile */
    }
}

int main(void)
{
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;
    uint8_t *p = malloc(16);         /* 违规：动态内存 */
    free(p);                         /* 违规：动态内存 */

    usart.USART_BaudRate = 115200;
    USART_Init(USART1, &usart);      /* 违规：全文件未见 USART 时钟使能 */

    nvic.NVIC_IRQChannel = USART1_IRQn;
    NVIC_Init(&nvic);                /* 违规：未见 NVIC_PriorityGroupConfig */

    while (1) {
        ;                            /* 空转：不涉及标志等待，不告警 */
    }
}
