# 智能排队闸机（FreeRTOS 原语全家桶教学样本 + LCD1602 首见）

> 源工程：`D:\stm32project\stm32_hal_rtos-master\stm32_hal_rtos-master\智能排队系统(RTOS)`（HAL+FreeRTOS，F103 @72MHz，正点原子 BSP 布局）。
> 状态：**文件级读码**，未编译未上板。业务体量小（~1000 行用户码），价值在 RTOS 原语的教科书级组合用法。

## 1. 工程档案

- 芯片：STM32F103（stm32f103.uvprojx），HSE 8MHz × PLL_MUL9 = 72MHz，FLASH_LATENCY_2，APB1=div2。
- 布局（正点原子风格）：`Users/`（main.c+FreeRTOSConfig.h+it.c）、`Drivers/BSP/`（beep/exti/freertos_test/gate/key/lcd1602/led——**业务逻辑也放 Drivers**）、`Drivers/SYSTEM/`（delay/sys/uart1）、`Middlewares/`（FreeRTOS）。
- main.c 仅 33 行：init 全家桶 → `freertos_test()` 建任务 → `vTaskStartScheduler()`。
- FreeRTOSConfig：tick 1kHz / 32 优先级 / 堆 10KB / 软件定时器开（定时器任务优先级 31=最高，栈 256 word）/ Mutex 开 / 栈溢出检测关。

## 2. 业务架构（排队闸机状态机）

```
PASS 态（闸机开，5 个通行权）
  红外检测有人通过(EXTI) → 取 1 个通行权 → 显示 PASS n/05
  5 个用完 → WAIT 态
WAIT 态（闸机关+蜂鸣+LED 快闪）
  3 秒后软件定时器到期 → 任务通知 sensor 任务
  → 信号量重置回 5 个 → PASS 态
```

## 3. FreeRTOS 原语清单（全家桶一次用齐）

| 原语 | 实例 | 职责 |
|---|---|---|
| 任务 ×3 | sensor(20ms 轮询)/device(10ms)/display(阻塞收) | 检测/执行/显示 |
| 队列 ×2 | state_queue(3×u8)/display_queue(3×结构体) | 状态解耦 / 显示请求 |
| 互斥锁 | lcd_mutex | LCD1602 单写者保护 |
| **计数信号量** | passage_semaphore = xSemaphoreCreateCounting(5,5) | **通行权资源池**（真实语义） |
| 软件定时器 | wait_timer 3s 单次 | WAIT 态超时 |
| **任务通知** | xTaskNotifyWait / xTaskNotify(eSetBits) | 定时器回调 → sensor 任务（**技能库首见实操**） |

## 4. 任务通知实操（本工程最有教学价值的一段）

```c
#define NOTIFY_WAIT_TIMEOUT 0x01   // 位域：bit0=等待超时
#define NOTIFY_PERSON_PASS  0x02   // bit1=有人通过（预留）

// 定时器回调（守护任务上下文，禁阻塞）→ 只发通知不干活
void wait_timer_callback(TimerHandle_t xTimer) {
    xTaskNotify(sensor_task_handle, NOTIFY_WAIT_TIMEOUT, eSetBits);
}

// sensor 任务轮询收通知（timeout=0 非阻塞，位域判读）
if (xTaskNotifyWait(0, UINT32_MAX, &notification_value, 0) == pdTRUE) {
    if ((notification_value & NOTIFY_WAIT_TIMEOUT) != 0) { ... }
}
```
- 与队列/信号量对比：任务通知零创建成本、直达任务、位域可复用一个 32 位值承载多事件——ISR/定时器回调→任务的轻量通道首选。
- 回调转交模式：软件定时器回调只做"通知"，业务逻辑回任务上下文执行——与"HAL 回调=ISR 只置标志"（坑 71）同一纪律。

## 5. 计数信号量资源池（drain-then-refill 重置）

```c
passage_semaphore = xSemaphoreCreateCounting(MAX_PASSAGES, MAX_PASSAGES); // 5,5
xSemaphoreTake(passage_semaphore, 0);       // 有人通过：消费 1 个通行权
// 超时重置：先排空再补满，防"手里还剩旧票"
while (xSemaphoreTake(passage_semaphore, 0) == pdTRUE);
for (i = 0; i < MAX_PASSAGES; i++) xSemaphoreGive(passage_semaphore);
```
- 计数信号量当资源池比"int 计数器"好在哪：Take/Give 天然原子，多任务并发取票无需临界区。
- 陷阱提示：drain-then-refill 只适用于单点重置场景；多消费者并存时 drain 会抢走别人正持有的语义。

## 6. 显示任务范式（队列+互斥双保护）

```c
while (1) {
    xQueueReceive(display_queue, &msg, portMAX_DELAY);      // 无消息即休眠
    if (xSemaphoreTake(lcd_mutex, portMAX_DELAY) == pdTRUE) {
        lcd1602_show_line(msg.row, msg.col, msg.message);   // 慢速外设独占写
        xSemaphoreGive(lcd_mutex);
    }
}
```
- 消息=定长结构体 `{row, col, message[16]}`（LCD1602 一行 16 字符定长，无动态内存）。
- 与 hal-freertos-field-guide 的显示串行化建议完全一致：任何任务想刷屏→投队列，显示任务是唯一写者。慢速外设（本例每字节 10ms）持锁 100+ms 的代价被"低频刷新"摊薄。

## 7. LCD1602 并口驱动（技能库首见 HD44780 类屏）

- 接线：数据 D0~D7 = **PA0~PA7 整口**；RS=PB1（0=指令/1=数据）、RW=PB0（常拉低只写）、EN=PB10（写脉冲）。
- **整口直写技巧**：`GPIOA->ODR = cmd;` 一条指令输出 8 位数据——比 8 次 WritePin 快且整齐；代价是整口独占（见 §8 冲突）。
- 初始化序列（不读忙信号 BF 的教学版）：上电 15ms → 0x38 ×2（8 位两行 5×7）→ 0x08 关显示 → 0x01 清屏 → 0x06 光标右移 → 0x0C 开显示。
- DDRAM 地址：行 1 = `0x80+col`，行 2 = `0x80+0x40+col`。
- 时序全部 `delay_ms(5)`（EN 前后各 5ms）——HD44780 实际只需 µs 级（使能脉冲宽度 ≥450ns）；一行 16 字符 ≈ 160ms，全屏 ≈ 320ms。教学级够用，产品级应换 µs 延时+BF 检测。
- 画面滚动指令 0x18（右移一位），演示用。

## 8. bug/瑕疵清单（validator + 人工）

1. **死码掩盖引脚冲突（坑 76）**：key.c 占 PA0/PA1 输入，lcd1602.c 把 PA0~PA7 整口配成推挽输出；初始化顺序 lcd 在 key 之后 → 按键脚被抢。但 `key_scan()` 全工程无人调用——冲突被死码掩盖，一旦启用按键即失效（读到的永远是 LCD 数据线电平）。
2. EXTI 回调 `HAL_GPIO_EXTI_Callback` 内 `delay_ms(20)` 消抖：delay 是 SysTick VAL 轮询忙等，EXTI4 优先级(2)高于 SysTick(最低) → **回调期间内核 tick 停 20 拍**（validator VIOLATION）。正解：ISR 只记 `HAL_GetTick()` 时间戳，任务侧判消抖。
3. `ia_flag_get()` 读清两步非原子（temp=flag; flag=FALSE）——ISR 在两句间置位会丢事件；xTaskNotifyFromISR 才是正解。
4. USART1_IRQHandler 自管收发（不走 HAL_UART_IRQHandler）：ISR 内 `printf` 回显 + `HAL_UART_Receive(…,1000)`（超时参数在 ISR 语境无意义）+ rx_buf/rx_len 非 volatile——正点原子模板通病（坑 68/71 家族），validator 4 VIOLATION 全真实。
5. `delay_ms` 忙等实现 + `HAL_Delay` 被重写为 delay_ms（正点原子 RTOS 方案：规避 HAL_Delay 依赖 SysTick 中断）——任务里长延时应改 vTaskDelay 释放 CPU。
6. 状态双通道冗余：system_state 全局变量 + state_queue 同时传状态（device 任务两处读）——单一事实来源应只留队列。

## 9. validator 记录 + 生成检查单

- 11 用户文件（--density md）：2 文件 6 VIOLATION 全真实（exti.c ×2：ISR 非 volatile + ISR 内 delay；uart1.c ×4：ISR 非 volatile ×3 + ISR 内 printf），其余全 PASS；freertos_test.c（RTOS 原语主战场）0/0 干净。
- 生成此类代码时自查：
  1. 定时器/ISR 回调只通知不干活：`xTaskNotify(handle, bits, eSetBits)` 转交任务上下文。
  2. 通知位域用 `#define NOTIFY_XXX (1<<n)` 宏集中定义，收发两端同文件引用。
  3. 计数信号量资源池：`Create(max, max)` 起满；消费 Take(0)、重置先 drain 再补满（仅限单点重置）。
  4. 显示任务 = portMAX_DELAY 阻塞收队列 + 互斥锁 + 定长消息结构体；其他任务禁直写屏。
  5. LCD1602 整口 ODR 直写前，先出全工程引脚表确认整口无他用（本工程反面教材：key 与 LCD 抢 PA0/PA1）。
  6. 软件定时器命令队列（configTIMER_QUEUE_LENGTH）与定时器任务栈按定时器数量复核。
  7. 死模块要么删掉要么在交付说明标注"预留未启用"——防死码掩盖资源冲突。
