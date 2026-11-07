/* === EMBED MANIFEST ===
module: usart1_echo
track: spl
mcu: STM32F103C8T6 (md)
pins: PA9 (USART1_TX, AF_PP), PA10 (USART1_RX, IN_FLOATING)
vectors: USART1_IRQHandler
clocks: RCC_APB2Periph_USART1, RCC_APB2Periph_GPIOA
depends: stm32f10x_rcc.c, stm32f10x_usart.c
=== END MANIFEST === */
/* tests 样本：与 lcd_key.c 故意争夺 PA9，验证跨文件引脚冲突聚合检查。
   单文件纪律保持 0 违规（聚合违规才是本样本的测试目标）。 */
#include "stm32f10x.h"

void usart1_echo_init(void)
{
    /* 省略：样本只测 manifest 聚合，不触发单文件规则 */
}

void usart1_echo_poll(void)
{
    /* 省略 */
}
