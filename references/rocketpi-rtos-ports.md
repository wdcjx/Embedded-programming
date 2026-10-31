# RocketPi 四 RTOS 移植（FreeRTOS / RT-Thread / ThreadX / Zephyr，F401RE）

> 源工程：`D:\stm32project\stm32f401re_rocketpi-master\stm32f401re_rocketpi-master`（rocketpi.club 教学套件，100+ 示例）。
> 状态：**文件级读码**，未上板未编译（本仓库 Agent 无 west/Studio 环境）。ThreadX/Zephyr 为技能库首见样本。

## 1. 工程总览

- 板卡：RocketPi，STM32F401RE（Cortex-M4F，512KB Flash / 96KB SRAM，主频 84MHz），板载 ST7789 240×240、W25Q64、ESP8266 座、SD 卡、I2S 功放、WS2812B、三色 LED（PA1/PB10/PB14 低有效）、按键 PA0。
- 生态：`factory`（厂测 lvgl+FATFS+unit_test）/ `base_samples`（42 个裸机 HAL 例程）/ `boot`（bootloader + ymodem IAP）/ `rocketpi_demo`（7 个综合项目：MQTT+OTA、USB 音频、智能家居等）/ `rtos`（四大 RTOS 移植）/ `hardware`（原理图 PDF + IO 分配 xlsx）/ mkdocs 文档站。
- 时钟：HSE 8MHz（STLink MCO 供源），PLL 4×84÷2 = 84MHz（F401 上限）。

## 2. 四 RTOS 移植对比（同板同硬件，构建/时钟/集成方式全景）

| 轨道 | 工程形态 | 内核版本 | 时钟方案 | HAL/驱动层 | 构建体系 |
|---|---|---|---|---|---|
| FreeRTOS | 3 套：cubemx_keil（CubeMX 自动生成）/ LTS-keil（手挂内核）/ LTS-GCC（CMake） | CubeMX 自带 + v202406.04-LTS | HSE 84MHz | STM32 HAL | CubeMX+MDK / CMake |
| RT-Thread | RT-Thread Studio 工程 | 5.1.0（完整源码内嵌） | **HSI（Studio 模板默认，未改）** | 官方 STM32 BSP 全套 drv_*.c | Studio（Eclipse 系） |
| ThreadX | 2 套：keil + gcc（同构 CubeMX 工程） | 6.4.3（ports/cortex_m4/keil + common/src） | HSE 84MHz（PLLN=84 P=2） | STM32 HAL | CubeMX+MDK / GCC |
| Zephyr | 板级移植 + 14 示例（HWMv2） | v4.3.99 | HSE 84MHz（dts：mul-n=168 div-p=4） | Zephyr 官方 STM32 驱动栈 | west/CMake |

要点：
- **四轨时钟不统一**：RT-Thread 轨 `drivers/board.h:45` `#define BSP_CLOCK_SOURCE ("HSI")` 是 Studio 生成模板默认值，作者未改；其余三轨均 HSE。同板不同固件时钟源不同，排查外设时序问题时要先确认跑的是哪一轨。
- FreeRTOS 三形态完整覆盖了技能库既有两篇文档的全部模式（CubeMX 自动挂 / 手动拷内核 / CMake），无新增知识点，此轮不重复展开。

## 3. ThreadX × CubeMX HAL 集成（技能库首见，核心增量）

### 3.1 结构
```
Core/Src/main.c                    ← 标准 CubeMX，末尾 tx_kernel_enter()
Core/Src/stm32f4xx_it.c            ← 三处改造（见 3.3）
Core/Src/stm32f4xx_hal_timebase_tim.c  ← HAL 时基迁 TIM10（CubeMX 自动）
Core/ThreadX/app_threadx.c         ← 用户线程定义
Core/ThreadX/tx_low_level.c        ← 板级初始化（关键文件）
Core/ThreadX/tx_user.h             ← TX_TIMER_TICKS_PER_SECOND 等
ports/cortex_m4/keil/*.s           ← ThreadX 官方汇编端口（PendSV/SysTick 接线）
common/src/*.c                     ← ThreadX 内核 C 源码
```

### 3.2 `_tx_initialize_low_level()` 逐行（tx_low_level.c:19-52）
```c
__disable_irq();
// 未用内存起点：ARMCC=Image$$RW_IRAM1$$ZI$$Limit；GCC=__bss_end__（字对齐）
_tx_initialize_unused_memory = (VOID *)unused_memory;
// 系统 MSP：ARMCC=__Vectors[0]（向量表首项）；GCC=_estack；顺手 SCB->VTOR=
_tx_thread_system_stack_ptr = ...;
NVIC_SetPriority(SVCall_IRQn,  0xFF);   // 最低
NVIC_SetPriority(PendSV_IRQn, 0xFF);    // 最低——上下文切换永不让外设等待
NVIC_SetPriority(SysTick_IRQn, 0x40);   // 内核 tick 中档
SysTick->LOAD = (SystemCoreClock / TX_TIMER_TICKS_PER_SECOND) - 1;
SysTick->VAL = 0; SysTick->CTRL = CLKSOURCE|ENABLE|TICKINT;
```
- 与 FreeRTOS 的差异：ThreadX 无 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 等价物（中断锁用 PRIMASK/BASEPRI 由端口决定，Keil 端口 `_tx_thread_interrupt_disable` 用 PRIMASK——所有中断一律让路）；SysTick 优先级 0x40 硬编码在板级文件，可按需改。
- **`__tx_SVCallHandler` 是 while(1) 占位**：CM 端口 ThreadX 不用 SVC 做任何事（上下文切换全走 PendSV），SVC 向量挂死循环是"永不进入"的防御位。FreeRTOS 恰好相反（SVC 启动第一个任务）。

### 3.3 it.c 三处改造（与 FreeRTOS×CubeMX 完全同构）
1. `PendSV_Handler` 整段注释（汇编端口提供 `__tx_PendSVHandler`，弱符号覆盖启动文件默认）；
2. `SysTick_Handler` USER 区加 `_tx_timer_interrupt();`（内核 tick）；
3. HAL 时基留在 `TIM1_UP_TIM10_IRQHandler` → `HAL_TIM_IRQHandler(&htim10)`（1ms，HAL 专用）。

### 3.4 线程创建模式（app_threadx.c）
```c
tx_thread_create(&th, "name", entry, 0, stack, sizeof(stack),
                 prio, /* 预抢占阈值 */prio, TX_NO_TIME_SLICE, TX_AUTO_START);
// entry: for(;;){ HAL_GPIO_TogglePin(...); tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND/2); }
```
- 第 6/7 参是 ThreadX 特有的**抢占阈值**（preemption-threshold）：同值=同优先级互相不抢占；栈用 `ULONG` 数组静态分配（无动态内存）；`TX_THREAD_NOT_USED()` 是作者自写的免警告宏。
- `tx_kernel_enter()` 之后的代码不可达（main.c 里跟一行 `Error_Handler()` 表达失败即停）。

## 4. Zephyr 板级移植（技能库首见，核心增量）

### 4.1 板注册五件套（`rocket_pi/`，HWMv2）
| 文件 | 作用 |
|---|---|
| `board.yml` | **HWMv2 权威**：name/vendor/socs（stm32f401xe） |
| `rocket_pi.yaml` | 旧 HWMv1 板描述（identifier 残留 `nucleo_f401re`，supported 列表，ram/flash）——HWMv2 下仅参考 |
| `Kconfig.rocket_pi` + `Kconfig.defconfig` | `BOARD_ROCKET_PI` select SOC；defconfig 里 `SPI_STM32_INTERRUPT=y` |
| `rocket_pi_defconfig` | 最小内核配置（MPU+HW_STACK_PROTECTION+SERIAL+GPIO+SENSOR） |
| `board.cmake` | 三个 runner（stm32cubeprogrammer/openocd/jlink） |
| `rocket_pi.dts` + 两个 connector dtsi | 全部硬件声明（下表） |

### 4.2 dts 声明速查（把"驱动开发"变成"填表"）
| 硬件 | dts 节点写法 |
|---|---|
| ST7789 屏 | `zephyr,mipi-dbi-spi` 泛生节点 + `sitronix,st7789v` 子节点（vcom/gamma/porch 全参数，`mipi-mode="MIPI_DBI_MODE_SPI_4WIRE"`） |
| HC-SR04 | `compatible="hc-sr04"` + trigger/echo-gpios（官方驱动直接用） |
| 蜂鸣器/舵机 | `pwm-leds` 泛化：`pwms=<&pwm3 3 PWM_HZ(2700) 0>` / `<&pwm3 4 PWM_HZ(50) 0>` |
| 软 I2C | `zephyr,i2c-bitbang` + scl/sda-gpios |
| W25Q64 | spi2 下 `jedec,spi-nor`（jedec-id=[ef 40 17]、has-dpd、fixed-partitions 8MB） |
| ESP8266 | **usart6 下挂 `espressif,esp-at` 子节点**=WiFi offload，应用层直接用 Zephyr 网络栈 |
| MCUboot | flash0 fixed-partitions：boot 64K / slot0 128K / slot1 128K / scratch 128K |
| USB CDC | `usbotg_fs` 下 `zephyr,cdc-acm-uart` |
| 时钟树 | `&pll{div-m=4;mul-n=168;div-p=4;}` + `&rcc{clock-frequency=<84MHz>;apb1-prescaler=<2>;}` |

### 4.3 构建（readme.md 实录）
```
west build -p always -b rocket_pi samples/basic/blinky     # 官方 36 例
west build -p always -b rocket_pi display                  # 自制 14 例（rocketpi_example/ 下）
west build -p always -b rocket_pi --sysbuild with_mcuboot  # MCUboot 组合构建
```
- 官方 36 例清单（blinky/threads/sensor_shell/spi_flash/fs/cdc_acm/shell/smf/rtio/cmsis_rtos_v2/tflite-micro 等）+ 自制 14 例（adc_joystick/aht30/at24c02/buzzer/display/esp8266/hcsr04/irda_nec/l9110h/max98357/radar/sg90/with_mcuboot/ws2812b）。
- 每个自制例四件套：`CMakeLists.txt` + `prj.conf` + `boards/rocket_pi.overlay`（板级覆写）+ `src/main.c`。
- esp8266 示例是 Zephyr 网络栈标准范式：`net_mgmt_init_event_callback` + `K_SEM_DEFINE` + `wifi_connect_req_params`（SSID/PSK/安全类型/超时）+ `net_config_init_app`。

## 5. RT-Thread Studio 工程结构

- 目录：`applications/main.c`（Studio hello 模板：`LOG_D` + `rt_thread_mdelay(1000)`，无外设示例）/ `drivers/`（官方 BSP 全套 drv_adc/can/clk/gpio/lcd/pwm/sdio/spi/usart/wdt…+ board.c/board.h）/ `linkscripts/STM32F401RE/` / `rt-thread/`（5.1.0 完整内核）/ `.settings/`（Eclipse 配置 + DAPLink/JLink/Qemu/STLink 四套调试 launch）。
- 与 Keil/CMake 轨的本质差异：**Studio 把内核、BSP 驱动、链接脚本、调试配置全部工程化管理**，用户只写 applications/；代价是被 IDE 绑定。

## 6. base_samples 中间件能力索引（未深读，生成时按此清单指路）

littlefs（内部 flash + W25Q64 两例）· mbedtls（AES/RSA/SHA256）· cjson 串口控灯 · microrl shell（补全/历史）· easylogger 彩色日志 · ymodem 收发 · USB CDC 虚拟串口 · USB MSC（SRAM 介质）· FATFS on SDIO · LVGL on ST7789+摇杆 · MultiButton 按键库 · libdriver 系列（at24cxx/mg58f18 雷达）· sd→i2s 音频+频谱 · 图片轮播 6fps。bootloader 两例（纯跳转 / ymodem IAP）对照 OV-Watch IAP 知识（坑 64/65）。

## 7. 瑕疵清单（非安全级，如实记录）

1. `rocket_pi.yaml` 的 identifier 残留 `nucleo_f401re`（从 Nucleo 板复制未改）——HWMv2 权威在 board.yml，不致错但易困惑。
2. RT-Thread 轨 `BSP_CLOCK_SOURCE ("HSI")` 未改（其余三轨 HSE）——同板时序行为不一致的根源。
3. ThreadX 工程它.c 里 `FPU_IRQHandler` 空壳（Keil --cpu=Cortex-M4.fp 自动挂向量，正常）。

## 8. validator 记录

- ThreadX Keil 用户层 8 文件（main/gpio/usart/it/msp/timebase/app_threadx/tx_low_level）`--density f4` **全部 PASS 0 VIOLATION 0 WARNING**（含 `_tx_timer_interrupt` 这类外部符号不误报）。
- Zephyr（devicetree API）与 RT-Thread（Studio/BSP API）轨**不适用**本 validator（HAL/SPL 轨专用），边界如实声明。

## 9. 生成检查单（Agent 产出此类代码时自查）

1. ThreadX 集成六步：拷 `ports/cortex_m4/<toolchain>` + `common/src` → IncludePath 两条 → it.c 三改（PendSV 注释/SysTick→`_tx_timer_interrupt`/时基留 TIM）→ tx_low_level.c 按 3.2 填 → tx_user.h 定 `TX_TIMER_TICKS_PER_SECOND` → main 末尾 `tx_kernel_enter()`。
2. ThreadX 线程一律静态栈（`ULONG` 数组）+ 抢占阈值=优先级起步；`tx_thread_sleep` 用 `TX_TIMER_TICKS_PER_SECOND/x` 表达时长。
3. Zephyr 加板：五件套齐 + 硬件全部 dts 声明（能用 compatible 就不写驱动）；示例走 overlay 不改板文件。
4. 同板多 RTOS 并存时，交付说明必须写明各轨时钟源与主频（防"同一外设两轨行为不同"之谜）。
5. ESP8266 类 AT 模组优先声明 `espressif,esp-at` 挂 UART（Zephyr）/走 netmgr（RT-Thread），避免手写 AT 状态机。
6. `west build -p always` 全量重编防止 stale 缓存；`--sysbuild` 组 MCUboot。
7. 四轨 HAL 时基互斥表：FreeRTOS=TIM(任意)/ThreadX=TIM10/Zephyr=SysTick 内核自管/RT-Thread=BSP 自管——同芯片混用时逐轨核对 `HAL_GetTick` 源头。
