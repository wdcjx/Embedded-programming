# SPL + FreeRTOS 集成规则（STM32F1 / ARM_CM3 port）

> 适用 FreeRTOS-Kernel **10.x ~ 11.x**。库源码不入技能：内核由用户按本清单放进工程，
> 技能负责生成 FreeRTOSConfig.h、任务代码与中断集成。
> 组合选择错了整套代码都是错的——生成前必须确认用户工程是「裸机 SPL」还是「SPL + FreeRTOS」。

## 1. 用户侧文件清单（Agent 生成代码时附在交付说明里）

内核（FreeRTOS-Kernel 仓库 `Source/`，按需裁剪）：

| 文件 | 必需 | 说明 |
|---|---|---|
| `tasks.c` `list.c` `queue.c` | 必需 | 调度核心 + 队列/信号量 |
| `portable/MemMang/heap_4.c` | 必需 | 内存方案（heap_4） |
| `portable/RVDS/ARM_CM3/port.c + portmacro.h` | 必需（Keil） | GCC 工程换 `portable/GCC/ARM_CM3/` |
| `timers.c` | 可选 | 用软件定时器才加（本技能模板 `configUSE_TIMERS=0`） |
| `event_groups.c` `stream_buffer.c` `croutine.c` | 可选 | 用到才加 |

Keil 工程 include path 三项：`Source/include`、`portable/RVDS/ARM_CM3`、FreeRTOSConfig.h 所在目录。
版本固定在 README；升级内核大版本时须复核 FreeRTOSConfig.h 与 port 目录。

## 2. 时基与向量接管（RTOS 轨的第一铁律）

- SysTick/PendSV/SVC 三个异常**归 FreeRTOS 内核**。接管方式：FreeRTOSConfig.h 里三行宏
  （已写入模板，**不要**改启动文件）：
  ```c
  #define vPortSVCHandler     SVC_Handler
  #define xPortPendSVHandler  PendSV_Handler
  #define xPortSysTickHandler SysTick_Handler
  ```
- **禁止**再出现：`SysTick_Config()` 调用、裸机 `delay_init()/millis()/SysTick_Handler`（templates/main.c 那套整体删除）；
- `stm32f10x_it.c` 及用户代码中**不得**再定义 `SVC_Handler/PendSV_Handler/SysTick_Handler`（与内核重名，链接冲突或静默覆盖）；
- 任务延时一律 `vTaskDelay(pdMS_TO_TICKS(ms))`；需要时间戳用 `xTaskGetTickCount()`。

## 3. 中断纪律（与裸机最大的差异点）

- `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4)`：**必须**、上电一次。FreeRTOS 的
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` 语义依赖全 4 位抢占分组；
- 只有抢占优先级**数字 ≥ 5**（即低于等于 MAX_SYSCALL）的中断才允许调用任何 RTOS API；
  优先级 0~4 的中断里调 RTOS API 会触发 configASSERT/卡死——这类中断只能操作外设寄存器+置软件标志；
- ISR 标准范式（任务通知为例）：
  ```c
  void EXTI0_IRQHandler(void)
  {
      static BaseType_t woken = pdFALSE;   /* 或局部；必须是 BaseType_t */
      if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
          EXTI_ClearITPendingBit(EXTI_Line0);
          vTaskNotifyGiveFromISR(s_key_task, &woken);  /* 只能用 FromISR API */
          portEND_SWITCHING_ISR(woken);                /* portYIELD_FROM_ISR 等价 */
      }
  }
  ```
- 任务级 API（`xQueueSend/xSemaphoreGive/xTaskNotifyGive/vTaskDelay...`）出现在 ISR 里是硬错误
  （前者应换 `...FromISR`，后者无 FromISR 版本直接禁止）——`validate_generated.py` 会抓；
- 临界区：任务里 `taskENTER_CRITICAL()/taskEXIT_CRITICAL()`；ISR 里只能
  `taskENTER_CRITICAL_FROM_ISR()`，且越短越好；不要再用裸机 `__disable_irq()` 包 RTOS 操作。

## 4. 内存预算（C8T6 = 20K RAM，很紧）

- heap_4：`configTOTAL_HEAP_SIZE ≤ 10K`；**所有任务栈/TCB/队列都从 heap 里出**；
- 任务栈单位是**字**：`configMINIMAL_STACK_SIZE 128` = 512B；用 printf 的任务 ≥ 256 字；
- MSP（启动文件 `Stack_Size`）只服务中断+调度器，0x400~0x600 足够；
- 估算：RAM ≈ 静态全局 + heap + MSP栈；三项接近 20K 必须提示换 RCT6/ZET6；
- `configCHECK_FOR_STACK_OVERFLOW=2` 与 `configUSE_MALLOC_FAILED_HOOK=1` 必开，钩子里死循环。

## 5. 与裸机模板（templates/main.c）的差异清单

| 裸机版 | RTOS 版 |
|---|---|
| `delay_init()` + `SysTick_Handler` + `millis()` | 全部删除，内核接管 SysTick |
| `HAL_GetTick` 式节拍 `millis()` 比较延时 | `vTaskDelay(pdMS_TO_TICKS())` |
| `NVIC_PriorityGroup_2` | `NVIC_PriorityGroup_4` |
| ISR 置标志 → 主循环轮询 | ISR `FromISR` API 唤醒任务（通知/队列/信号量） |
| main 大 while(1) 主循环 | main 只做初始化 + `xTaskCreate` + `vTaskStartScheduler()` |

## 6. RTOS 特有坑（生成前过一遍）

1. heap 配小了 → 进 `vApplicationMallocFailedHook` 死循环（现象：上电串口无输出）；
2. 任务栈给小了 → 随机硬错误（开 OVERFLOW=2 才能抓到钩子）；
3. ISR 用任务级 API → 断言/硬错误（validator VIOLATION）；
4. 优先级 0~4 的 ISR 里调 FromISR API → configASSERT 触发；
5. 漏了 `portEND_SWITCHING_ISR(woken)` → 不报错，但唤醒延迟到下个 tick（实时性劣化）；
6. `configUSE_TIMERS=0` 却调用 `xTimerStart` → 链接错误；要用软件定时器先开 `configUSE_TIMERS=1` 并加 `timers.c`；
7. 裸机 `printf` 任务栈不足 → 栈溢出（printf 家族局部开销大，任务栈 ≥ 256 字）；
8. 两个任务都 `GPIOx->ODR ^= pin` → RMW 竞争（坑清单 #16）：多位/多任务共享 GPIO 用 SetBits/ResetBits（BSRR 原子）。

## 7. Keil 集成 SOP（2025-10 于用户 SPL 工程 D:\stm32project\6 实测通过，Kernel V11.3.1）

按序执行，每步对应一个可预期现象：

1. **拷文件**：内核 `tasks.c/list.c/queue.c/(timers.c)/heap_4.c + portable/RVDS/ARM_CM3/port.c+portmacro.h` 进
   `<工程>\FreeRTOS\src\`，`include\*` 进 `FreeRTOS\include\`，FreeRTOSConfig.h 进 User\；
2. **Keil 加组**：新组（建议名 FreeRTOS）→ Add Existing Files 加 src 下全部 .c/.h；
3. **Include Paths 两条都要**：`.\FreeRTOS\include`（内核头）+ `.\FreeRTOS\src`（portmacro.h——
   Keil **不会**自动搜索 .c 所在目录，缺这条 port.c 报 `portmacro.h: No such file or directory`）；
   FreeRTOSConfig.h 靠现有 User 路径即可；
4. **F7 全工程构建**（不是 Ctrl+F7 单文件编译）→ 预期 3×`L6200E multiply defined`
   （SVC/PendSV/SysTick_Handler，port.o 与 stm32f10x_it.o）——正常，进入下一步；
5. **删 it.c 三个 Handler 整函数** → F7 → 预期 3×`L6218E Undefined symbol`（三钩子）——正常；
6. **补三个钩子实现**（放 main.c 末尾）→ F7 → `0 Error(s)` 链接通过。

### 钩子签名（V11.3.1 实测，照抄内核原型，不得自创）

```c
void vApplicationMallocFailedHook(void);
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void vAssertCalled(const char *pcFile, int line);   /* projdefs.h 原型 (const char*, int) */
```

> 踩过的坑：vAssertCalled 第二参数写成 `unsigned long` 或第一参数少 `const` 都会
> `error #147-D: declaration is incompatible`——**报错信息里会印出官方原型**，照抄即可。
> 钩子实现放 main.c 末尾；文件末尾保留换行（否则 #1-D 警告）。

### 内核来源示例（用户本机）

FreeRTOS-LTS 仓库结构：`FreeRTOS-LTS\FreeRTOS\FreeRTOS-Kernel\`（manifest.yml 标注版本）。
LTS 202604.01 = **Kernel V11.3.1**，在本文档兼容区间内；AWS 系库（coreMQTT/Plus-TCP 等）暂不用。
