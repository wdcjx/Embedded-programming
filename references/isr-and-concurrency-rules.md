# ISR 与并发纪律

## 1. IRQHandler 唯一正确形态（标志位模式）

```c
volatile uint8_t  uart_frame_ready = 0;   /* ISR 写、主循环读：必须 volatile */
volatile uint8_t  rx_byte = 0;

void USART1_IRQHandler(void)              /* 名字与启动文件向量表逐字符一致 */
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        rx_byte = (uint8_t)USART_ReceiveData(USART1);  /* 读 DR 即清 RXNE */
        uart_frame_ready = 1;                          /* 只置标志 */
    }
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET) {
        (void)USART1->SR;                 /* 先读 SR */
        (void)USART1->DR;                 /* 再读 DR，才清 IDLE/ORE */
        uart_frame_ready = 1;
    }
}
```

ISR 里允许的三件事：**取数据、清标志、置标志**。
禁止：delay/忙等、printf、锁步等待外设、调用不可重入的复杂函数。

## 2. volatile 规则

必须 volatile 的变量：

1. ISR 写、主循环读（或反过来）；
2. DMA 与 CPU 共享的缓冲区与计数；
3. 忙等循环条件中读取的变量（`while (!ready);`——不加 volatile 会被编译器优化成死循环）。

volatile 不够用的情况：变量超过 CPU 原子宽度（见下节）或需要"读-改-写"时，volatile 只保证不优化，不保证原子。
多字段结构体被 ISR 写、主循环读：除整个结构体加 volatile 外，主循环读取时必须用临界区或"序号+副本"模式，否则会读到撕裂状态（新 cnt 配旧 buff）。

生成代码里与中断共享的结构体一律按此写：

```c
volatile DATA_RCV_STR rcv;          /* ISR 写，主循环读 */

/* 主循环侧读取一份一致副本 */
uint32_t primask = __get_PRIMASK();
__disable_irq();
DATA_RCV_STR snap = rcv;            /* 结构体拷贝要短 */
__set_PRIMASK(primask);
```

## 3. Cortex-M3 原子性

- **天然原子**：对齐的 8/16/32 位单次读、单次写（`uint8_t/uint16_t/uint32_t` 各一个）。
- **不原子**：
  - 读-改-写：`flags |= 0x01;`、`cnt++;`（读、改、写三步，可被中断打断）；
  - 多字节序列：64 位变量、结构体、多变量一致性（帧头+长度+载荷）。
- 修复手段：
  1. **单一位的置位/清零用 BSRR**：`GPIO_SetBits/ResetBits` 本身就是原子的（SPL 已封装）；
  2. **位带/多步序列用 PRIMASK 临界区**：

```c
uint32_t primask = __get_PRIMASK();
__disable_irq();
/* 临界区：读-改-写或多变量一致性操作，越短越好 */
__set_PRIMASK(primask);       /* 恢复进入前状态，不会误开已关的中断 */
```

  临界区不要嵌套关两次开一次（用上面的保存/恢复写法）；临界区内禁止任何等待。

## 4. 环形缓冲区所有权（无锁安全规则）

单生产者（ISR）单消费者（主循环）的 ring buffer 可以不加锁：

- ISR 只写 `head`，主循环只写 `tail`；
- 判空 `head == tail`，判满保留一个空位 `(head+1) % size == tail`；
- 缓冲区本体和两个索引都 volatile；元素宽度 ≤ 32 位保证索引读写原子。
多生产者/多消费者不适用此规则，必须加临界区。

## 5. 优先级与抢占

- 上电先 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);`（全工程一次）；
- 抢占优先级高的 ISR 可以打断低的；同抢占级不互相打断；
- 约定：时间敏感（电机控制、采样）> 通信 > 按键/指示；亚优先级只决定排队顺序；
- ISR 应短；长活搬到主循环标志位处理。

## 6. 向量名必须与启动文件一致

打开工程所用启动文件（如 `startup_stm32f10x_md.s`），搜索 `IRQHandler` 得到合法名字全集。
`USART1_IRQHandle`（少一个 r）这类拼写错误**编译链接都不报错**，但中断永远不触发。
`validate_generated.py` 会按密度宏校验名字。

**反向规则**：`Handler/IRQ` 后缀只留给真正的向量表函数。带参的通用处理函数（如 `void uart2_feed(USART_TypeDef *u)`）不要命名为 `xxHandler`——人和检查工具都会把它误认成中断向量，真向量反而被遮住。通用函数用 `_process/_poll/feed/_isr_task` 等后缀，由真向量薄封装调用：

```c
void USART2_IRQHandler(void)   /* 向量：只做转发 */
{
    uart2_feed(USART2);
}
```

## 7. 标志清除顺序速查

| 标志 | 清除方法 |
|---|---|
| EXTI Line | `EXTI_ClearITPendingBit(EXTI_Linex)`（写 1 清除） |
| TIM 更新/捕获 | `TIM_ClearITPendingBit(TIMx, TIM_IT_xxx)` |
| USART RXNE | 读 `USART_ReceiveData()`（读 DR）即清除 |
| USART IDLE / ORE | **先读 SR，再读 DR**（两步缺一不可） |
| ADC EOC | 读 `ADC_GetConversionValue()` 即清除 |
| I2C 事件 | `I2C_CheckEvent` 读序列天然清除；错误用 `I2C_ClearFlag` |
