#include "stm32f10x.h"
int main(void) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    return 0;
}
