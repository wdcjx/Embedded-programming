# GRBL (AVR) → STM32F407 + FreeRTOS 移植笔记

> 实测来源：CoreScribe 写字机（Hui404/CoreScribe，MIT/GPL-3.0 双许可）。
> F407VET6 @168MHz（HSE 8M, PLLM=4/N=168/P=2）、GRBL 内核 V1.1h 级 + FreeRTOS V10.4.3（CMSIS-RTOS V2）+ LVGL V8.3、STM32CubeIDE/GCC 工具链。
> 文件级实测；未上板复现，按范式参考。

## 1. 为什么 GRBL 能搬进 RTOS：架构判读

GRBL 原版是「两个中断 + 一个协作式死循环」的裸机程序：

| 原版 (AVR) | 本工程 (F407) |
|---|---|
| Timer1 OCFRA 中断 = 步进节拍 | TIM2 更新中断，ARR 运行时改实现变频 |
| Timer2 OCFRA 中断 = 脉宽复位 | TIM3 更新中断（PSC=84-1/ARR=10 → 10µs 后拉低 STEP） |
| Timer0 = 系统时基 | SysTick 共用（HAL + RTOS 双喂，见 §4） |
| serial RX/TX 中断 | USART1 中断（HAL_Init 之外手写环缓，见 §5） |
| PCINT 引脚变化 = 限位/控制键 | EXTI（限位 PC1/2/3、控制键 PB4~7） |
| main() 协作死循环 | grblTask 任务里跑同一个循环 |

**要点：GRBL 的实时性全部在中断里，主循环只是解析/规划**——这决定了它进 RTOS 后只需把主循环放进一个任务，中断层保持「不碰 RTOS API、只置标志」，内核架构不用动。移植类似单核强实时库前，先画这张「中断 vs 主循环」职责表。

## 2. 双任务划分与栈预算

```c
// freertos.c（CMSIS-RTOS V2，CubeMX 生成骨架 + 手改）
defaultTask: osPriorityNormal,  stack 4KB   // LVGL UI 全部调用收在这一处
grblTask:    osPriorityNormal1, stack 2KB   // GRBL 主循环，优先级略高于 UI
```

- UI 任务栈给 4KB：LVGL 建控件、snprintf 文本格式化都在任务栈上，2KB 会溢出。
- GRBL 略高一级是刻意设计：运动中 UI 卡顿可接受，反过来不可。
- **跨任务通道用手写环形队列 + 关中断临界区**（`ui_jog.c`：`__disable_irq()` 包住 head/tail 更新），没上 xQueue——小结构体浅拷贝场景可行，两个任务各持 head/tail 的所有者方向固定（UI 只写、GRBL 只读），属于「ISR↔主循环环形缓冲所有权」规则的任务间版本。
- 队列里传的是**拼好的 G 代码字符串**（如 `G90G21G0Z5`），出队后经 `serial_manual_Write()` 注入 GRBL 自家 RX 环缓——UI 层完全不知道 GRBL 内部结构，解耦干净。

## 3. 时序定时器移植：ARR 动态改 = 变频

GRBL 每个规划段速度不同，原版靠改 AVR 定时器分频；本工程的等价物：

```c
// stepper.c：弹出段时
__HAL_TIM_SET_AUTORELOAD(&htim2, st.exec_segment->cycles_per_tick - 1);
__HAL_TIM_SET_COUNTER(&htim2, 0);
```

- TIM2 PSC=84-1（84MHz Timer 时钟 → 1MHz 计数），上限节拍 10kHz，改 ARR 即改节拍频率。
- 脉宽：置位 STEP 引脚后启动 TIM3（ARR=10 → 10µs），更新中断里拉低 + 停表。**用两个定时器替代 AVR 的「比较中断 A 置位 + 比较中断 B 复位」组合**，逻辑等价且省去 OCR 配比换算。
- TIM2/TIM3 NVIC 优先级 6/7（数值大 = 优先级低），均 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)，合法；限位 EXTI 用 0（最高），**比 5 高就绝不能调 FromISR API**——回调里只调 `mc_reset()`（纯置 volatile 标志 + 关步进），符合纪律。
- 步进 ISR 里 `busy` 标志防重入 + 提前 `__enable_irq()` 允许高优先级嵌套——保留原版设计。
- 舵机抬落笔复用 Z 轴语义：步进 ISR 检测「仅 Z 在动」→ 按方向发 PEN_DOWN/PEN_UP 脉宽。写字机类应用（笔/Z 轴 = 抬落笔）可直接抄这个思路。

## 4. cli/sei 宏与 RTOS 临界区取舍

```c
// main.h
#define cli()  __set_PRIMASK(1)   // AVR 总中断关 → PRIMASK
#define sei()  __set_PRIMASK(0)
```

- PRIMASK 方案忠实还原 AVR 语义（**连优先级 0 的限位 EXTI 都关掉**），改动最小。
- RTOS 友好做法是 `taskENTER_CRITICAL()`（BASEPRI=5 屏蔽，高优先级中断仍活着）。两者都合法；**移植强实时库时先问「关中断期间允不允许最高优先级中断被拖延」**——限位安全场景宁可选 PRIMASK。
- system.c 控制键状态机多处 `cli(); …; sei();` 短临界区，符合「临界区只保护指针/标志交换，不放耗时操作」。

## 5. HAL_UART_IRQHandler 与手写 RXNE/TXE 的共存机制

`serial_init()` 用 CubeMX 句柄 + 寄存器混写：`huart1.Init.BaudRate=115200`、`USART1->CR1 |= RXNEIE`、禁 TXE。`stm32f4xx_it.c` 的 `USART1_IRQHandler` 先调 `HAL_UART_IRQHandler(&huart1)`，再在 USER CODE 区手写 GRBL 收发。

**为什么不打架**：HAL 的 RXNE 分支内部走 `UART_Receive_IT()`，它**只在 `RxState == BUSY_RX` 时消费 DR**；从没调过 `HAL_UART_Receive_IT()` 的工程 RxState 恒为 READY → HAL 空转，DR 完整留给手写逻辑。

⚠️ 条件共存：一旦某天调用 `HAL_UART_Receive_IT()`，HAL 开始抢 DR，手写分支读到的就是残缺流。两种收发模型不许混用（同 hal-driver-patterns §「同外设 HAL/LL 混用禁止」）。

手写 ISR 内容 = GRBL 原版实时命令解析（`!`~`0x7F` 扩展 ASCII 的 switch 置执行标志），30 行纯置标志，ISR 停留时间可控——原版设计本就在串口 ISR 里，不算「ISR 跑业务」违规。

## 6. 协作式死循环 → RTOS 任务的让步点（关键改造）

GRBL 主循环是裸机 busy-loop，直接进任务会饿死同核所有低优先级任务。本工程在 `protocol_execute_realtime()` 末尾加：

```c
// protocol.c：运动状态下每 5ms 让出 1 tick
static void protocol_background_slice(void) {
    if (!(sys.state & (STATE_CYCLE|STATE_HOLD|STATE_JOG|STATE_HOMING|STATE_SAFETY_DOOR))) return;
    uint32_t now = osKernelGetTickCount();
    if ((uint32_t)(now - last_slice_tick) < 5U) return;
    last_slice_tick = now;
    osDelay(1);
}
```

- 只有运动态让步（空闲态本来就在等串口），运动平滑性几乎不受影响。
- **遗留坑**：SLEEP 分支的 `while (!(sys.abort)) { protocol_exec_rt_system(); }` 没插让步——grblTask 优先级更高，SLEEP 时 UI 任务会被完全饿死。本工程 UI 用不到 SLEEP 故未触发；移植他人代码时，**每一个原版忙等 while 都要过一遍「加让步 or 换事件」**。
- 反面印证：`st_go_idle()` 里的 `if (__get_IPSR() != 0U) { st_deenergize_pending = 1U; return; }`——在中断上下文只置「待办」标志，回任务上下文再真正执行（`st_finalize_idle()`）。**`__get_IPSR()` 判上下文 + pending 延后执行**是中断安全化的标准范式，值得全文照抄。

## 7. LVGL V8.3 + FreeRTOS 集成三件套

1. **tick 不用再喂**：`lv_conf.h` 打开 `LV_TICK_CUSTOM 1` + `LV_TICK_CUSTOM_SYS_TIME_EXPR (xTaskGetTickCount())`——LVGL 时钟直接取 RTOS tick，TIM5 里手写的 `lv_tick_inc(1)` 因此注释废弃。
2. **显示/触摸初始化搬进 UI 任务**：`main()` 只做 `lv_init()`；`lv_port_disp_init()/lv_port_indev_init()` 在 defaultTask 开头调（FSMC LCD 初始化耗时 + 上电触摸误触问题都在任务里处理）。
3. **`lv_timer_handler()` 返回值即休眠建议**：`osDelay(ui_lvgl_task_step())`，返回值下限钳 1ms 防 `osDelay(0)` 空转。

## 8. 延时双模：DWT + 调度器感知

`MyDelay.c`（与 HAL_Driver_Lib core_delay 同源思想）：

- `MyDelay_us()`：DWT->CYCCNT 忙等（微秒级必须忙等，RTOS tick 1ms 太粗）。
- `MyDelay_ms()`：`xTaskGetSchedulerState() == taskSCHEDULER_RUNNING` 时 `vTaskDelay()`，否则 `HAL_Delay()`——**同一入口在调度器启动前后都安全**，初始化代码与任务代码可共用。
- FPU：`configENABLE_FPU 0`（CubeIDE 该宏语义≠关硬件 FPU），`main()` 里手动 `SCB->CPACR |= (3UL<<20)|(3UL<<22);` 打开 CP10/CP11 协处理器访问——若启动后浮点指令 HardFault，先查 CPACR。

## 9. EEPROM 与引脚规划

- GRBL 参数区 → 外挂 AT24C64（8KB，地址高 5 位掩码 `(addr>>8)&0x1F`）。软 I2C（GPIO 模拟，每 bit 100µs）+ **写后 Acknowledge Polling**（轮询应答代替盲等 5ms，超时 1000 次）。比「片内 Flash 模拟 EEPROM」省心：无擦写寿命分配、无中断向量搬家问题。
- cpu_map.h 引脚布局纪律（原版约束保留）：STEP 三根同端口（PA1/2/3）、DIR 同端口（PA4/5/6）、限位同端口（PC1/2/3 共 EXTI 分发）、控制键同端口（PB4~7，EXTI4 + EXTI9_5 共享）。**同端口 = ISR 里一次写整组**；本工程为可移植性改成逐引脚 `HAL_GPIO_WritePin`，牺牲少量速度换清晰度，TMC 驱动够用。
- 原版位掩码（bit2/bit3/bit4…）用兼容宏映射 GPIO_PIN_x，内核代码零改动。

## 10. 移植检查单（照做清单）

- [ ] 画「中断 vs 主循环」职责表，中断层保持不碰 RTOS API
- [ ] cli/sei 决策：PRIMASK（忠实/连高优先级也关）还是 BASEPRI（RTOS 友好）
- [ ] 所有原版忙等 while 逐个审查：插 `osDelay` 让步点或换事件/通知
- [ ] ISR 里调的每个函数过 `__get_IPSR()` 检查，做不到安全的改 pending 模式
- [ ] 定时器移植：节拍表用 ARR 动态改；脉宽用第二个定时器更新中断
- [ ] 中断优先级表对照 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`：≥ 它才能调 FromISR
- [ ] HAL 中断处理器与手写寄存器逻辑共存前，确认 HAL 消费条件（RxState 等）
- [ ] 延时入口做调度器感知分流；微秒级延时用 DWT
- [ ] 大初始化（屏、EEPROM 探测）放任务里，main 只留 `lv_init` 级轻活
- [ ] 编译器/链接脚本、栈高水位、堆预算复核（本工程 heap 15KB/192KB RAM，保守合理）
