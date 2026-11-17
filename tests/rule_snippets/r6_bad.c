#include "stm32f10x.h"
void EXTI4_IRQHandler(void) { while (flag_ready()) { } }
int flag_ready(void) { return 0; }
