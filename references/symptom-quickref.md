# 症状速查表（按现场表现反查根因）

> 遇到「死机 / 挂死 / 卡住 / 数据乱 / 偶发异常」类描述，先在此表反查，再进入对应文档细读。
> 来源：用户《STM32实战避坑手册》v1.0 附录 B；「章节」列已映射到技能内文档。

## 症状 → 最可能根因

| 现场表现 | 最可能的根因 | 细读 |
|---|---|---|
| 某个中断一触发，整机停住，uwTick 不再变化 | ISR 或回调里调用了 HAL_Delay()（SysTick 被压制无法递增） | hal-basics.md §5、hal-freertos-field-guide.md §1 前 |
| 串口打印正常，一加接收中断就挂 | __HAL_LOCK 重入或提前返回未解锁（回调里续接 Receive_IT 是经典） | hal-freertos-field-guide.md §1 |
| 重新生成代码后，自己写的函数不见了 | 代码写在 USER CODE 区域之外（CubeMX 覆盖） | hal-basics.md §3 |
| DMA 配置正确，数据就是乱的 | Cache 一致性（F7/H7）或缓冲区未 32B 对齐 | hal-freertos-field-guide.md §10 |
| 调度器启动后卡在第一个任务里 | 优先级分组错误、三 Handler 重复/静默空实现、SysTick 被屏蔽 | freertos-spl-integration.md §7、hal-freertos-field-guide.md §9 |
| 偶发 HardFault、队列数据错乱、不良率几个点 | 调 FromISR 的 ISR 优先级越界（数值 < 阈值） | freertos-spl-integration.md、hal-freertos-field-guide.md §9 |
| 偶发卡死，逻辑上查不出原因 | 二值信号量做互斥导致优先级反转 | hal-freertos-field-guide.md §6 |
| 调度器一启动就 HardFault | RTOS 堆耗尽且未检查创建返回值（NULL→空指针） | hal-freertos-field-guide.md §8 |
| 任务数据被莫名改写，崩溃点在别处 | 栈溢出，或 DMA 缓冲区放在任务栈上 | hal-freertos-field-guide.md §7 |
| 低功耗待机电流超标几十倍 | 悬空 GPIO 浮空输入 + 未关外设时钟 | hal-freertos-field-guide.md §11 |
| 函数不返回，看门狗复位 | 外设时钟未使能，或 HSE 起振死等（无超时） | hal-freertos-field-guide.md §4 |
| HAL 函数返回 HAL_TIMEOUT 但外设看着正常 | HAL 时基被 RTOS 临界区屏蔽，或时基未启动 | hal-freertos-field-guide.md §4、freertos-hal-integration.md |
| 主循环莫名停顿上百毫秒 | 阻塞收发（低波特率 1KB≈1s）或 HAL_MAX_DELAY 超时 | hal-freertos-field-guide.md §2 |
| 明明初始化了却返回 BUSY / 配置被莫名覆盖 | 同一外设 HAL 与 LL 混用，句柄状态失真 | hal-freertos-field-guide.md §3 |

若两条以上同时命中，先处理排在前面的——它往往是其他现象的原因。

## 三条排查原则

1. **先确认时间基准**：uwTick 是否在动、时基中断是否在跑。时间基准不对，所有超时/延时/调度现象都不可信；
2. **先看返回值，再看现象**：HAL_BUSY→并发与锁；HAL_TIMEOUT→时钟/引脚/时基；HAL_ERROR→外设配置。返回值是最便宜的证据；
3. **先固定变量，再改代码**：中断优先级、缓冲区大小、任务顺序、优化等级列出来，一次只改一个。最怕「顺手改了三处，好了，但不知道为什么好」。
