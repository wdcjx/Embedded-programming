/* === EMBED MANIFEST ===
module: tim3_pwm
track: spl
mcu: STM32F103C8T6 (md)
pins: PA6 (TIM3_CH1, AF_PP)
vectors: (none)
clocks: RCC_APB1Periph_TIM3, RCC_APB2Periph_GPIOA
depends: stm32f10x_rcc.c, stm32f10x_tim.c
=== END MANIFEST === */
/* tests 样本：与 key_exti.c 引脚/向量均不冲突，聚合应 PASS。 */
#include "stm32f10x.h"

void tim3_pwm_init(void)
{
    /* 省略：样本只测 manifest 聚合，不触发单文件规则 */
}
