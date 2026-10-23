# HAL 与 FreeRTOS 现场可靠性指南（从「实验室能跑」到「现场不死机」）

> 基准来源：用户整理的《STM32实战避坑手册_HAL与FreeRTOS》v1.0（2026-10，内部实测资料，F1/F4/H7、HAL v1.12–v1.18、FreeRTOS V7.5+）。
> 本文档只收录技能其他文档未覆盖的**增量**；向量接管/FromISR 阈值/时基剥离/USER CODE 区等见 freertos-spl-integration.md、freertos-hal-integration.md、hal-basics.md。
> 核心论断：**HAL 的超时语义（忙等 uwTick）和锁语义（非阻塞 BUSY）都是为裸机轮询设计的，搬进 RTOS 会变成死锁与优先级反转的温床**。

## 1. __HAL_LOCK：非阻塞 API 里的隐形死锁（⭐ 最高频新坑）

HAL 的 IT/DMA 收发函数入口有一把锁（不是互斥量，是标志位）：

```c
/* __HAL_LOCK(handle) 的真实语义：不可嵌套、不可等待、失败即返回 */
if (handle->Lock == HAL_LOCKED) return HAL_BUSY;   /* 已锁：立刻失败 */
handle->Lock = HAL_LOCKED;                          /* 上锁，直到完成/出错回调解锁 */
```

三种经典触发：
1. **收发同时进行**：主循环刚发起发送，中断接收完成又立刻调 Receive_IT，句柄仍 LOCKED；
2. **⭐ 回调里续接下一包**：`HAL_UART_RxCpltCallback()` 里再调 `HAL_UART_Receive_IT()`——回调本身在解锁之前执行，这是最经典重入；
3. **提前 return 忘解锁**：某分支直接返回没走 `__HAL_UNLOCK()`，句柄永久 BUSY。

- 症状辨识度极高：**「轮询发送正常，一加接收中断就挂」「打印正常，接第二路数据就不进回调」**，返回值 HAL_BUSY 但多数人不检查；
- DMA 版同样有锁：连续两次 `HAL_UART_Transmit_DMA()` 第二次直接 BUSY——**非阻塞 ≠ 可排队**；
- RTOS 里更危险：`while (HAL_UART_Transmit(...) == HAL_BUSY)` 变成占 CPU 的忙等，且持锁的中断回调永远不会被调度到；
- 老手纪律：不在同一外设的回调里递归调用该外设收发函数；**每处 HAL 调用都检查返回值**；通信层用 DMA+空闲中断+环形缓冲，业务放任务里；注释掉 `__HAL_LOCK` 是拆保护不是修复，之后并发安全须自己的 Mutex 保证。

## 2. 阻塞式收发的时间成本（数据决策）

| 波特率（8N1） | 单字节约耗时 | 发送 1 KB 约耗时 |
|---|---|---|
| 9600 | 1.04 ms | **1.07 s** |
| 115200 | 86.8 µs | 89 ms |
| 921600 | 10.9 µs | 11 ms |

- `HAL_UART_Transmit()` 是带超时的 while（等 TXE→写 DR→等 TC）；超时填 `HAL_MAX_DELAY` 时，TXE 永不置位（时钟没使能/引脚没配/波特率寄存器错）就永久卡死；
- 纪律：超过几十字节或实时性要求的数据走 DMA；阻塞版本必须**有限超时**（如 100ms）并检查 `HAL_TIMEOUT/HAL_ERROR`；调试期翻转 GPIO 用示波器实测占用时间。

## 3. HAL vs SPL 开销实测 与 HAL/LL 混用边界

同一颗 F103C8T6、不加优化的实测量级：

| 指标 | SPL | HAL | 差异 |
|---|---|---|---|
| GPIO 翻转频率 | 1.64 MHz | 1.08 MHz | 低约 34% |
| 代码体积 | 1.0× | 2.6× | +160% |
| 中断响应 | < 10 µs | 可达 50 µs | 最多 5 倍（向量→IRQHandler→标志判断→回调分发） |

- 老手**分层**：初始化/时钟/复杂外设用 HAL；中断与高频路径（GPIO 翻转、SPI 字节搬运）用 LL 或寄存器；中间层用 `__HAL_XXX` 宏；
- ⭐ **铁律：同一个外设实例禁止 HAL 与 LL 混用**——HAL 句柄保存 State/ErrorCode/Lock，LL 直改寄存器 HAL 不知情，表现为「明明初始化了却 BUSY」或「配置被莫名覆盖」；跨外设混用安全。同一外设先用 HAL 初始化再交 LL 跑热路径可以，但此后不再调该外设任何 HAL 函数。

## 4. 启动阶段的三处隐性卡死（与中断/RTOS 无关）

1. **HSE 起振死等**：`while (!HSERDY)` 无超时——晶振虚焊/负载电容不配/受潮时直接卡死。加计数超时并回退 HSI；
2. **HAL_InitTick 失败被忽略**：`HAL_Init()` 内部调用的返回值没人看——时基没起来 uwTick 恒 0，之后所有 HAL 超时函数永久等待；
3. **外设时钟漏使能**：等标志位永远不置位，调试器里表现为「函数不返回」。

**排查第一原则：上电先 watch uwTick。不动先查时基，别查业务。**

## 5. 临界区分层使用（不要一把梭）

| 上下文 | 正确 API | 屏蔽范围 |
|---|---|---|
| 任务 | `taskENTER/EXIT_CRITICAL()` | BASEPRI，只屏蔽受管理中断（数值≥阈值） |
| ISR | `taskENTER_CRITICAL_FROM_ISR()` | 同上，保存/恢复原 BASEPRI |
| 裸机惯性 | `__disable_irq()` | **全部中断含 SysTick——RTOS 里禁用**（节拍丢失） |

- F103@72MHz 实测临界区开销约 62ns；50µs 的临界区让高优先级中断最多延迟 50.8µs——可接受，但前提是真短；
- 只圈共享资源那几行，不包整个函数；临界区内禁任何可阻塞/切换 API（vTaskDelay、带阻塞时间的 xQueueSend、malloc）；
- **任务版与 ISR 版不可混用**：ISR 里调任务版会破坏嵌套计数 uxCriticalNesting，任务侧退出永远对不上。

## 6. 共享外设用 Mutex，不用二值信号量（优先级反转）

- 场景：低优先级 A 持锁做慢速 I2C → 中优先级 B 就绪抢占 → 高优先级 C 等锁——**C 的等待时间由无关的 B 决定**，表现为「偶发卡死，逻辑上查不出原因」；
- `xSemaphoreCreateMutex()` 自带**优先级继承**（持锁者临时提到等待者优先级）；二值信号量没有；
- ⭐ **配置澄清：不存在 `configUSE_PRIORITY_INHERITANCE` 宏**——优先级继承随 Mutex 内建，由 `configUSE_MUTEXES=1` 决定（教程里流传的写法别照抄）；
- **Mutex 不能在 ISR 里 Take/Give**；ISR 同步用二值信号量/任务通知的 FromISR 版本；
- 递归获取用 `xSemaphoreCreateRecursiveMutex()`；普通 Mutex 重复 Take 死锁；
- 持锁期间禁 `vTaskDelay()` 和 HAL 阻塞函数（`HAL_I2C_Mem_Read` 超时可能几百毫秒，所有等锁任务全挂）——超时压到毫秒级或改 DMA+回调；
- 多把锁约定统一获取顺序，否则互相等待=教科书死锁。

## 7. 任务栈精算：word 单位 + 64 字节固定开销

- `xTaskCreate()` 栈深单位=**word**（填 128=512B）；反着写 1024 当字节用=分配 4KB；
- **每个任务固定上下文开销 64 字节**：切换时硬件压 8 寄存器（R0-R3,R12,LR,PC,xPSR）+ port 存 R4-R11 = 16 个寄存器；**M4F/M7 用浮点的任务更多**（硬件压浮点上下文+port 可能再存 S16-S31）；
- 中断嵌套用当前任务的 PSP 压栈——栈余量按**最坏中断嵌套深度**算，不是平均负载；
- 流程：开发期 `configCHECK_FOR_STACK_OVERFLOW=2` + 溢出钩子 → 跑完全部路径（printf、%f、HAL 复杂函数开销完全不同量级）→ `uxTaskGetStackHighWaterMark()` 读历史最低余量（单位 word）→ **留 30%–50% 余量**定终值；
- 方法 2 只查栈底 20 字节填充图案（已踩底才发现）；高水位才是预警——两者一起用；
- **DMA 缓冲区绝不放任务栈上**（可能写到栈外/覆盖 RTOS 堆，崩溃点在几个任务之外）。

## 8. RTOS 堆预算：算上所有内核对象

从 `configTOTAL_HEAP_SIZE` 分配的不止任务栈：TCB、队列存储区、信号量/互斥量、软件定时器、任何 `pvPortMalloc()`。
- 堆不够时 `xTaskCreate/xQueueCreate` 返回 NULL——不检查返回值，调度器一启动就 HardFault（内核拿到空指针）；
- `configUSE_MALLOC_FAILED_HOOK=1` + `vApplicationMallocFailedHook()` 让堆耗尽可观测；
- `xPortGetFreeHeapSize()` 看当前、**`xPortGetMinimumEverFreeHeapSize()` 看历史最低（判断够不够的依据）**；
- 内存确定性要求高时：`configSUPPORT_STATIC_ALLOCATION=1` + `xTaskCreateStatic()`，占用在 .bss、map 文件可查。

## 9. FreeRTOS 落地顺序：先立规矩，再跑业务

1. NVIC 分组 Group_4（任何 NVIC_Init/SetPriority 之前）；
2. 三 Handler 唯一（grep 全工程确认，weak 静默链接的空实现也要防——「调度器启动了但不切换」比链接报错更难查）；
3. 配置 MAX_SYSCALL 阈值，逐个核对 FromISR 中断优先级（**建议留一级余量：阈值 5 就设 6**——实测案例：H7 项目优先级 5 与阈值 5 撞车导致调度停滞）；
4. 开 configASSERT 跑最小系统（两任务+一队列+一信号量）；
5. 开栈溢出检查+malloc 钩子，跑通全部业务路径；
6. 最后才关断言出固件。「把断言关掉硬调 = 把有导航的路走成盲人摸象——80% 移植坑本可十分钟定位」。

## 10. 带 D-Cache 的型号（F4 部分/F7/H7 预埋，F1 无此问题）

DMA 与 CPU 看到的可能不是同一份内存：发送时数据停在 Cache 里 DMA 取旧值；接收后 Cache 旧行挡住新数据。表现为「DMA 配置全对，数据就是乱的」且与缓冲区大小相关。

```c
__attribute__((aligned(32))) static uint8_t tx_buf[N];   /* 32B = M7 Cache line */
SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, N);           /* 发送前 Clean */
SCB_InvalidateDCache_by_Addr((uint32_t*)rx_buf, N);      /* 接收后 Invalidate */
```

- 顺序不能反；对齐是硬要求（不对齐时 Invalidate 连坐相邻变量）；双向缓冲各做一次；**volatile 替代不了 Cache 维护**；
- 一劳永逸：MPU 把 DMA 区配成 Non-cacheable（需吃透链接脚本）；多任务下把 Clean+启动 / 等待+Invalidate 收敛到同一任务/驱动层。

## 11. 低功耗坑（真实教训：待机电流超标 75 倍）

根因=CubeMX 默认开启的 ADC 时钟 + 悬空 GPIO（浮空输入随电平摆动，输入级反复翻转漏电）。
- 未用 GPIO 配模拟输入或固定电平，**不浮空**；
- 关闭不用外设的时钟（`__HAL_RCC_xxx_CLK_DISABLE`），不只是不初始化；
- 进低功耗前确认 ADC/比较器/内部参考源都进预期状态。

## 12. FreeRTOSConfig.h 最小配置集（与本文问题直接相关）

| 配置项 | 建议值 | 说明 |
|---|---|---|
| configUSE_PREEMPTION | 1 | 实时性要求下不用协作式 |
| configUSE_MUTEXES | 1 | Mutex 与优先级继承的开关 |
| configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY | 5 | FromISR 逻辑值下限（留余量设 6） |
| configKERNEL_INTERRUPT_PRIORITY | 15<<4 | SysTick/PendSV 用，移位后的寄存器值 |
| configTICK_RATE_HZ | 1000 | 1 tick=1ms，osDelay 换算直观 |
| configCHECK_FOR_STACK_OVERFLOW | 2 | 开发期必开 |
| configUSE_MALLOC_FAILED_HOOK | 1 | 堆耗尽可观测 |
| configASSERT | 见下 | 移植期必开，出固件前才关 |

能真正停下来的 configASSERT（配合调试器直接看违规中断与优先级）：

```c
static void app_assert_failed(void) { taskDISABLE_INTERRUPTS(); for(;;){} }
#define configASSERT(x)  do { if ((x) == 0) app_assert_failed(); } while (0)
```

## 13. 常见误解对照（照抄教程会引入新问题）

| 流传说法 | 实际情况 |
|---|---|
| `HAL_Delay(0)` 立即返回 | 至少等约 1 个 tick（1kHz 时基=1ms） |
| volatile 能解决 DMA 与 Cache 问题 | 只约束编译器；Cache 需显式维护 |
| `NVIC_SetPriority` 可传 configKERNEL_INTERRUPT_PRIORITY | CMSIS 收未移位逻辑值；传已移位值被二次移位截断，通常变 0（最高优先级，与意图相反） |
| 要开 configUSE_PRIORITY_INHERITANCE | 不存在此宏；优先级继承随 Mutex 内建（configUSE_MUTEXES） |
| 二值信号量可当互斥量 | 同步可以；做互斥无优先级继承→优先级反转 |
| 开了栈检测就说明栈够用 | 只在切换点检查，发现时可能已踩坏数据；高水位才是预警 |
| HAL 与 LL 可随意混用 | 跨外设可以；同一外设实例混用让句柄状态失真 |
