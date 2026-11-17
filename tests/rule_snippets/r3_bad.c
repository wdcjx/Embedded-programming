#include "stm32f10x.h"
char rx_byte;
void EXTI1_IRQHandler(void) { rx_byte = 1; }
