/* bad_sample_hal.c — HAL 轨故意违规样本（自测 validate_generated.py）
 * 预期违规：
 *   1. HAL 回调访问非 volatile 全局（rx_byte / frame_ready）
 *   2. 回调内 HAL_Delay（铁律二：回调=中断上下文，禁止阻塞/延时）
 *   3. MX_GPIO_Init 缺时钟使能（无 __HAL_RCC_GPIOC_CLK_ENABLE）
 */
#include "stm32f1xx_hal.h"

UART_HandleTypeDef huart1;
uint8_t rx_byte;        /* 违规：被回调写、主循环读，缺 volatile */
uint8_t frame_ready;    /* 违规：同上 */

/* HAL 回调运行在中断上下文 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    rx_byte = (uint8_t)(huart->Instance->DR & 0xFFU);
    frame_ready = 1U;
    HAL_Delay(1);       /* 违规：回调内延时，时基为 SysTick 时会死锁 */
}

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    /* 违规：漏了 __HAL_RCC_GPIOC_CLK_ENABLE() */
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
}
