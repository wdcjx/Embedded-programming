# HAL + FreeRTOS 完整集成（CubeMX 实测基准）

> 实测锚点：`D:\STM32CubeMX\text2`——STM32F103C8T6 + CubeMX FW_F1 **V1.8.7**（CubeMX 6.18.1）+ FreeRTOS 中间件 + **HAL 时基=TIM2** + MDK-ARM。
> validator 回归：main.c / freertos.c / stm32f1xx_hal_timebase_tim.c / stm32f1xx_it.c 四文件 **0 违规 0 警告**。
> 生成代码时可对照该工程；手写非 CubeMX 工程按 §8 清单搬。

## 1. CubeMX 配置路径

| 配置 | 位置 | 实测值 |
|---|---|---|
| HAL 时基 | SYS → Timebase Source | **TIM2**（默认 SysTick；选 RTOS 后必须换 TIM，选没用的外设） |
| RTOS 接口 | Middleware → FREERTOS → Interface | **CMSIS_V1**（FW_F1 V1.8.7 默认；新工程可选 CMSIS_V2） |
| RTOS heap | FREERTOS → Total Heap Size | 3072（CubeMX F1 默认 3KB） |
| 任务 | Tasks01 | defaultTask + myTask02，栈 128（单位=**字**，128×4=512B），Dynamic |

.ioc 佐证：`NVIC.TimeBase=TIM2_IRQn`、`VP_SYS_VS_tim2`、`VP_FREERTOS_VS_CMSIS_V1.Mode=CMSIS_V1`。

## 2. 生成文件地图（谁在哪——改造工程先看这张）

| 文件 | 内容 |
|---|---|
| `Core/Src/main.c` | 外设初始化、**任务创建调用**（osThreadDef/osThreadCreate，V1 全在 main.c）、**时基回调 `HAL_TIM_PeriodElapsedCallback`（USER CODE 区，`if (htim->Instance == TIM2) HAL_IncTick();`）** |
| `Core/Src/freertos.c` | `vApplicationGetIdleTaskMemory` 静态内存钩子（configSUPPORT_STATIC_ALLOCATION=1 时生成） |
| `Core/Src/stm32f1xx_hal_timebase_tim.c` | **`HAL_InitTick()` 重写**：`htim2` 句柄、按 APB1 分频算 TIM 时钟、`HAL_TIM_Base_Start_IT` 启动 1ms 中断、设 `uwTickPrio` |
| `Core/Src/stm32f1xx_it.c` | 只有 `TIM2_IRQHandler`（转发 `HAL_TIM_IRQHandler(&htim2)`）与 Fault Handler——**没有 SVC/PendSV/SysTick_Handler（CubeMX 生成时就不写）** |
| `Core/Inc/FreeRTOSConfig.h` | 三宏接管 + heap 3072 + `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5` |
| `Middlewares/Third_Party/FreeRTOS/Source` | 内核源码（CubeMX 管理，别手改） |

## 3. 时基 TIM2 数据流（1ms tick 链路）

```
TIM2 1ms 中断 → it.c TIM2_IRQHandler → HAL_TIM_IRQHandler(&htim2)
             → main.c HAL_TIM_PeriodElapsedCallback → HAL_IncTick() → uwTick++
（与 RTOS 完全独立；SysTick 专用于内核 tick）
```

注意：**改时基频率要同时改 timebase_tim.c 的 Prescaler/Period**（默认按 1kHz 算好）；别把 TIM2 挪作它用（PWM/编码器都不行，tick 是它的唯一职责）。

## 4. 向量接管联动细节（⭐ 手写工程最容易错的点）

FreeRTOSConfig.h 底部三宏（与 SPL 轨集成一致）：

```c
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* CubeMX 原文注释：时基=SysTick 时本宏必须注释掉，防止覆盖 HAL 的 SysTick_Handler */
#define xPortSysTickHandler SysTick_Handler
```

- **时基=TIM2 时**：三个宏全启用，it.c 不定义这三个函数——CubeMX 自动保证；
- **时基=SysTick 时**：`xPortSysTickHandler` 宏被 CubeMX 注释（内核不接 SysTick，HAL 自己用）——此时 RTOS tick 从哪来？CubeMX 会生成 `HAL_ResumeTick`/ORE 转发……实际用得极少，**规范做法就是时基换 TIM**；
- 手写工程必须两边同时保证：宏启用 + it.c 无同名函数，缺一就是 L6200E 或 tick 全错。

## 5. CMSIS_V1 vs CMSIS_V2 API 对照

| 动作 | V1（text2 实测） | V2 |
|---|---|---|
| 内核启动 | `osKernelStart()` | `osKernelInitialize()` → … → `osKernelStart()` |
| 任务定义 | `osThreadDef(name, fn, prio, instances, stack)` | `osThreadNew(fn, arg, &attr)` |
| 任务句柄 | `osThreadId` | `osThreadId_t` |
| 延时 | `osDelay(ticks)` | `osDelay(ticks)` |
| 创建在 | main.c 的 MX_…_Init 之后 | freertos.c 的 MX_FREERTOS_Init 内 |

生成代码按工程实际 API 写（看 main.c 用哪套），别混用。

## 6. 内存模型：两套堆别混淆

| 堆 | 定义处 | 实测值 | 用途 |
|---|---|---|---|
| **RTOS heap** | FreeRTOSConfig.h `configTOTAL_HEAP_SIZE` | 3072 | 所有 Dynamic 任务/队列/定时器 |
| **C 库堆** | startup `.s` 的 `Heap_Size` | 0x200 (512B) | printf/malloc（MicroLIB 用得少） |

F103C8 只有 20K RAM：heap 3072 + 两个 512B 任务栈 + 内核对象，加传感器缓冲就紧张——任务栈从 128 字起步按需加，别拍脑袋给 512。栈溢出排查用 `configCHECK_FOR_STACK_OVERFLOW=2` + vApplicationStackOverflowHook（CubeMX 生成的 FreeRTOSConfig 默认含钩子声明，实现在 freertos.c USER CODE 补）。

## 7. 任务/中断纪律（validator 实测覆盖）

- 任务内延时只用 `osDelay`/`vTaskDelay`——`HAL_Delay` 虽然还能走（TIM2 独立时基）但它是忙等 uwTick 轮询，白烧 CPU，**任务内禁用**；
- ISR 内禁一切 `osXxx` 非 FromISR API；中断优先级抢占值 ≥5（`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`）才能调 FromISR；TIM2 时基本身的优先级 CubeMX 设为 15（最低，不抢业务）；
- 调度器启动后 `main` 里 `osKernelStart()` 不返回，其后代码不可达；
- validator 对本工程四文件 0/0 全过：`timebase_tim.c` 的 `HAL_InitTick` 重写不算 SysTick 违规（HAL 机制本身），`it.c` 的 `TIM2_IRQHandler`→`HAL_TIM_IRQHandler` 转发不误报。

## 8. 手写（非 CubeMX）HAL+FreeRTOS 集成清单

1. 内核文件（=CubeMX Middlewares 同款）：tasks/list/queue/timers.c + heap_4.c + `portable/RVDS/ARM_CM3/port.c+portmacro.h`
2. FreeRTOSConfig.h 用技能模板（三宏接管 + Group_4 预算）
3. 自己写 `stm32f1xx_hal_timebase_tim.c`（或从 CubeMX 生成工程拷），`HAL_InitTick` 重写 + 启动 TIM
4. it.c：**删** SVC/PendSV/SysTick_Handler（如果从别的工程拷来的），留/加 `TIM2_IRQHandler`
5. main.c：`HAL_Init()`（内部会调 HAL_InitTick→TIM2）→ 时钟 → 外设 → 任务创建 → `osKernelStart()`
6. main.c USER CODE 区补 `HAL_TIM_PeriodElapsedCallback`（`Instance==TIM2 → HAL_IncTick`）
7. NVIC 分组 Group_4（HAL 默认，确认没被覆盖）
