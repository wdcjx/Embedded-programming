/* === EMBED MANIFEST ===
module: key_exti
track: spl
mcu: STM32F103C8T6 (md)
pins: PB0 (KEY1, IPO)
vectors: EXTI0_IRQHandler
clocks: RCC_APB2Periph_GPIOB, RCC_APB2Periph_AFIO
depends: stm32f10x_rcc.c, stm32f10x_gpio.c, misc.c
=== END MANIFEST === */
/* tests 样本：EXTI0 向量名合法（md 表内），与 tim_pwm.c 无引脚交集，聚合应 PASS。 */
#include "stm32f10x.h"

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}
