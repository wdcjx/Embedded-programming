#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
void EXTI2_IRQHandler(void) { vTaskDelay(1); }
