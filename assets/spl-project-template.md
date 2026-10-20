# SPL Keil 工程模板（组织 / 配置 / 占用估算）

> 本文件描述"代码之外"的工程纪律：目录组织、编译配置、体积估算、点验流程。

## 1. 目录组织（推荐）

```
Project/
├── User/
│   ├── main.c                  # 业务入口（骨架用 embed/templates/main.c）
│   ├── bsp_xxx.c/.h            # 按外设拆分的初始化/驱动（bsp_usart、bsp_led…）
│   ├── stm32f10x_conf.h        # 按需放开所用外设头文件
│   ├── stm32f10x_it.c          # 或将 IRQHandler 集中于此（全工程一份）
│   └── system_stm32f10x.c      # 标准库自带，确认 SYSCLK_FREQ_72MHz
├── Libraries/
│   ├── CMSIS/                  # core_cm3.h、stm32f10x.h（校对 HSE_VALUE）
│   └── STM32F10x_StdPeriph_Driver/   # 只添加用到的 stm32f10x_xxx.c
├── startup/
│   └── startup_stm32f10x_md.s  # 按密度选 LD/MD/HD
└── Output/                     # .hex/.map 等
```

## 2. Keil 工程必配项（新建工程检查）

- [ ] Device = STM32F103C8（或实际型号）；启动文件密度与 Flash 匹配（C8T6 → MD）
- [ ] C/C++ Define：`STM32F10X_MD, USE_STDPERIPH_DRIVER`
- [ ] `HSE_VALUE` 与板载晶振一致（默认 8000000）
- [ ] Target 勾选 **Use MicroLIB**（printf 用 fputc 重定向时必需）
- [ ] C99 Mode 勾选（变量随用随声明）
- [ ] `stm32f10x_conf.h`：只放开实际用到的 `#include "stm32f10x_xxx.h"`，加了的头必须对应添加驱动 `.c`

## 3. stm32f10x_conf.h 模板

```c
#ifndef __STM32F10x_CONF_H
#define __STM32F10x_CONF_H

/* 按需放开：用哪个外设开哪个，对应的 stm32f10x_xxx.c 必须已在工程里 */
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_usart.h"
/* #include "stm32f10x_tim.h"  */
/* #include "stm32f10x_adc.h"  */
/* #include "stm32f10x_i2c.h"  */
/* #include "stm32f10x_spi.h"  */
/* #include "stm32f10x_exti.h" */
/* #include "stm32f10x_dma.h"  */
#include "misc.h"                /* NVIC_Init 所在 */

#endif
```

## 4. Flash / RAM 占用估算

编译后看 `.map` 或输出窗口：

- **Flash ≈ Code + RO-data + RW-data**
- **RAM ≈ RW-data + ZI-data**（ZI 含静态缓冲区与栈/堆）

纪律要求：交付时给出估算行，例如
`预计 Flash 6.2KB / RAM 1.8KB（C8T6 64KB/20KB，余量充足）`；
RAM 超 70% 或栈 >2KB 时必须提示风险（中断嵌套深、局部大数组都要吃栈）。

## 5. 首板上电点验顺序（bring-up）

1. 静态检查：电源 3.3V、RESET 拉高、BOOT0=0、晶振起振；
2. 烧"点灯 + 串口打印版本号/编译时间"的最小固件，确认最小系统；
3. MCO(PA8) 可选输出 SYSCLK/2 用示波器确认 72MHz；
4. 再逐个启用业务外设，每加一个外设跑一次 `validate_generated.py`；
5. 全功能联调时接上 IWDG（喂狗点写在主循环稳定路径），验证卡死能自愈。

## 6. FreeRTOS 接入（SPL + FreeRTOS 组合，Kernel 10.x~11.x）

工程目录追加：

```
Project/
├── Middlewares/FreeRTOS/
│   ├── source/            # tasks.c list.c queue.c + include/
│   │   └── portable/      # RVDS/ARM_CM3(port.c, portmacro.h) + MemMang/heap_4.c
│   └── FreeRTOSConfig.h   # 用 templates/FreeRTOSConfig.h
└── User/main_rtos_spl.c   # 用 templates/main_rtos_spl.c（替代裸机 main.c 的节拍部分）
```

- Keil include path 追加：`Middlewares/FreeRTOS/source/include`、`Middlewares/FreeRTOS/source/portable/RVDS/ARM_CM3`、FreeRTOSConfig.h 所在目录；
- 只加用到的内核 .c：必需 tasks/list/queue/heap_4/port 五件；timers.c/event_groups.c/stream_buffer.c 按需；
- **RAM 重新核算**：RW+ZI 里要加上 `configTOTAL_HEAP_SIZE`（C8T6 建议 ≤10K）+ MSP 栈；
- 完整规则（向量接管、FromISR、优先级红线）见 `references/freertos-spl-integration.md`。

## 7. 多文件模块化布局（真实工程验证过的两种风格）

**风格 A（本技能默认）**：每外设一个 `bsp_xxx.c/.h`，`stm32f10x_it.c` 集中所有 IRQHandler，main 只做 init + 调度。

**风格 B（智慧农业工程实测）**：模块自带中断——`User/<module>/<module>.c` + 同名 `.h`（led、key、adc、dht11、oled、SGP30、mqttwifi…），通信类模块在自己的 .c 里定义 `USARTx_IRQHandler`；`sys.c` 集中 `NVIC_Config()`（分组 + 全部 NVIC_Init），main 只 init + 主循环调度。采用风格 B 的约束：

- 全工程每个向量名仍然只能出现一次（validate 多文件模式会查重）；
- NVIC_Init 允许分散在各模块，但 **NVIC_PriorityGroupConfig 必须全工程一次**（集中在 sys.c 最先调用）；
- 模块对外只暴露 `xxx_init()/xxx_task()/xxx_get()` 类接口，跨模块共享数据走 `.h` 里的 extern volatile + 临界区；
- 生成新模块时给用户明确的"加入 Keil 工程"步骤：Project 窗口对应 Group → Add Existing Files，C/C++ include path 加模块目录。
