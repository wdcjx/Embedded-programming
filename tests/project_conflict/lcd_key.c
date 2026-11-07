/* === EMBED MANIFEST ===
module: lcd_key
track: spl
mcu: STM32F103C8T6 (md)
pins: PA0 (LCD1602_RS, OUT_PP), PA1 (LCD1602_EN, OUT_PP), PA2 (LCD_D4, OUT_PP), PA3 (LCD_D5, OUT_PP), PA4 (LCD_D6, OUT_PP), PA9 (LCD_D7, OUT_PP)
vectors: (none)
clocks: RCC_APB2Periph_GPIOA
depends: stm32f10x_rcc.c, stm32f10x_gpio.c
=== END MANIFEST === */
/* tests 样本：LCD1602 四线模式占用整口时把 PA9（D7）也占了，
   与 usart_echo.c 的 USART1_TX 冲突——坑 76 场景的机器可读版。
   单文件纪律保持 0 违规（聚合违规才是本样本的测试目标）。 */
#include "stm32f10x.h"

void lcd_key_init(void)
{
    /* 省略：样本只测 manifest 聚合，不触发单文件规则 */
}

void lcd_key_scan(void)
{
    /* 省略 */
}
