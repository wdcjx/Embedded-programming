#include "stm32f10x.h"
void led(void) { GPIO_SetBits(GPIOD, GPIO_Pin_13); }
