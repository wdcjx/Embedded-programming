#include "stm32f10x.h"
void delay_ms(int);
void EXTI3_IRQHandler(void) { delay_ms(1); }
