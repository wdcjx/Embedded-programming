#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
int main(void) {
    SysTick_Config(72000);
    xTaskCreate(0, 0, 0, 0, 0, 0);
    return 0;
}
