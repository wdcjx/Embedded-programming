/**
 * stm32f10x_conf.h — 编译 CI 用的最小外设配置头。
 *
 * SPL 的 stm32f10x.h 在定义 USE_STDPERIPH_DRIVER 时会 #include 本文件；
 * 模板/示例自身不带 conf.h（真实工程由用户工程提供），编译 CI 用这份
 * 最小集覆盖模板用到的外设头。多 include 不影响正确性（都是声明）。
 */
#ifndef __STM32F10X_CONF_H
#define __STM32F10X_CONF_H

#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_exti.h"
#include "stm32f10x_flash.h"
#include "misc.h"

#endif /* __STM32F10X_CONF_H */
