# 已知坑清单（症状 / 原因 / 修法）

> 触及本清单任何条目的生成代码，必须在交付说明中显式申报（SKILL.md 工作流第 5 步）。

1. **波特率/延时全错**
   症状：串口乱码、delay 时长不对。原因：`stm32f10x.h` 的 `HSE_VALUE` 与板载晶振不符（默认 8000000）。修法：按实际晶振改宏，重算确认 `SYSCLK_FREQ_72MHz`。

2. **F1 ADC 数值漂移/偏差大**
   原因：未校准。修法：`ADC_Cmd` 后执行 ResetCalibration→等待→StartCalibration→等待（顺序见 checklist §6）。

3. **PA15/PB3/PB4 配了 GPIO 没反应**
   原因：默认被 JTAG 占用。修法：开 AFIO 时钟 + `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`。

4. **EXTI 进不了中断**
   原因：忘开 `RCC_APB2Periph_AFIO`；或没做 `GPIO_EXTILineConfig`；或 NVIC 使能前挂起位已置位（一使能就进一次假中断）。修法：补 AFIO、补映射、使能前 `EXTI_ClearITPendingBit`。

5. **TIM1/TIM8 PWM 无输出**
   原因：MOE 未置位。修法：`TIM_CtrlPWMOutputs(TIMx, ENABLE)`。

6. **TIM 时钟频率算错**
   原因：APB1 分频≠1 时定时器时钟 = APB×2（F103 定时器多为 72MHz，不是 36MHz）。修法：按"×2 规则"重算分频。

7. **串口收几个字节后不再收**
   原因：ORE（溢出）标志未清，接收停止。修法：中断里同时处理 ORE/IDLE——先读 SR 再读 DR；不要只用轮询 RXNE 还在主循环里干别的太久。

8. **F1 硬件 I2C BUSY 常亮、总线卡死**
   原因：F1 硬件 I2C 的老问题，从机拉死 SDA 后无法恢复。修法：优先软件模拟 I2C；或复位时把 SCL 手动打 9 个时钟释放从机；所有事件等待必须带超时并做总线恢复。

9. **SPI1 与 TIM3 引脚冲突**
   症状：两个外设都"时好时坏"。原因：PA6/PA7 同时分配给了 SPI1(MISO/MOSI) 和 TIM3 CH1/CH2。修法：引脚分配表阶段查重。

10. **IWDG 误重启 / 形同虚设**
    原因：超时算错（LSI 40kHz，注意分频）；或喂狗放在定时中断里——主程序卡死时照样喂。修法：按 checklist 计算 Reload；喂狗只放主循环稳定路径。

11. **C8T6 大数组/深调用导致启动即硬错误**
    原因：20KB RAM 耗尽（栈溢出、DMA 缓冲区过大）。修法：估算 RW+ZI；必要时改启动文件 `Stack_Size/Heap_Size`；大缓冲区静态全局。

12. **Keil printf 卡死**
    原因：未勾选 Use MicroLIB，fputc 落入半主机模式。修法：勾 MicroLIB 或实现完整 retarget。

13. **SysTick_Config 返回值没检查**
    原因：主频过高时 Reload 超过 24bit，配置失败但代码继续跑。修法：`if (SysTick_Config(...)) { 错误处理 }`。

14. **NVIC 分组未配置/配置多次**
    原因：依赖默认分组、或多个 init 文件各自配了一次。修法：main 最先配一次 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2)`，其余地方禁止再配。

15. **外设句柄/配置结构体放栈上被中断引用**
    原因：局部变量出作用域即失效。修法：被 ISR、DMA、回调引用的一切必须是全局/静态。

16. **"复用寄存器读-改-写"被中断撕开**
    症状：偶发位丢失，难复现。原因：`GPIOx->ODR ^= pin` 这类 RMW 被中断中的同寄存器操作打断。修法：改用 BSRR（GPIO_SetBits/ResetBits）；其他外设用 PRIMASK 临界区。

以下 17~24 条来自真实工程（智慧农业 F103C8 SPL，2026-10 回归）实测发现：

17. **国内 Keil 工程源码多为 GBK 编码**
    症状：Agent/脚本工具按 UTF-8 读报错或中文注释乱码；新文件存 UTF-8 后旧版 Keil 里中文注释显示乱码。修法：改既有工程时先探测编码（读出 `invalid UTF-8` 即按 GBK 处理），读写保持同一编码；新建文件先问用户 Keil 的编码设置。

18. **vsprintf/sprintf 写缓冲无边界**
    原因：`vsprintf(buf, fmt, ap)` 不检查长度，格式串一长就栈/全局区溢出（真实案例：u2_printf 向 256 字节缓冲）。修法：一律 `vsnprintf(buf, sizeof(buf), fmt, ap)`，并检查截断。

19. **`__align(8)` 是 ARMCC 专属**
    症状：代码换编译器（GCC/IAR）编不过。修法：ARMCC 用 `__align(8)`，GCC 用 `__attribute__((aligned(8)))`；跨编译器代码用宏封装。同类专属还有 `__weak`（GCC 可用但语义细节不同）。

20. **ISR 写、主循环读的共享结构体没有 volatile/临界区**
    症状：偶发收到脏数据，难以复现。原因：结构体多个字段（cnt/buff/时间戳）被 ISR 写、主循环读，只加 volatile 仍可能读到撕裂状态（读到新 cnt 旧 buff）。修法：共享结构体声明 volatile + 多字段读取用 PRIMASK 临界区或"序号+副本"模式；只读单个标量才允许裸 volatile。（真实案例：ESP8266 接收结构体。）

21. **带参通用函数命名带 Handler/IRQ 后缀**
    危害：`USARTx_RCVHandler(USART_TypeDef*)` 这类普通函数会被人和检查工具误认为中断向量；真向量反而被薄封装遮住。修法：中断向量名严格保留给向量表函数；通用处理函数命名用 `_process/_poll/feed` 等后缀，且不放在 isr 命名空间。

22. **ISR 里调业务函数 + 先做事后清标志**
    风险：TIM 中断里调 KeyScan() 之类的业务函数，被调函数内部耗时/再调 delay 不可控；清标志放在业务之后，业务耗时长会导致标志重挂、重入。修法：ISR 只做"取数据+置标志"；确需在 ISR 内调函数时审计整条调用链耗时；外设标志先清再处理（防重入）。

23. **SPL 工程用 HAL 风格 shim 兼容驱动**
    技巧：delay.c 提供 `HAL_GetTick()/HAL_Delay()` 的 SPL 实现可让 HAL 风格驱动直接跑。风险：日后真正混入 HAL 库会符号冲突；SysTick_Handler 归 shim 所有，全工程不可再定义第二份。交付时必须申报存在 shim。

24. **时序协议传感器（DHT11 等）连续采样失败**
    症状：轮询读总是返回 0 或校验错。原因：DHT11 两次转换间隔必须 ≥1s（内部补偿需要时间），密集轮询必然失败。修法：驱动内做限流（距上次成功采样不足 1s 直接返回缓存值），调用方按慢周期调度。

25. **CubeMX 工程里重复定义 MspInit**
    症状：链接报 duplicate symbol（HAL_UART_MspInit 等）。原因：CubeMX 把 MspInit 集中生成在 stm32f1xx_hal_msp.c，往 main.c 或自建模块再写一份同名函数即冲突。修法：外设时钟/引脚改动写进 stm32f1xx_hal_msp.c 对应函数；新外设配置优先用 CubeMX 图形界面配好再生成，不手写生成区。

26. **拿 SPL 工程习惯套 CubeMX 工程**
    三个差异：①启动文件——CubeMX 给 C8/CB 用 startup_stm32f103xb.s（xb 命名=中密度），不是 SPL 的 startup_stm32f10x_md.s，自动放好不必自备；②JTAG 释放——CubeMX 默认 Serial Wire 且在 HAL_MspInit 自动写 __HAL_AFIO_REMAP_SWJ_NOJTAG()，无需 SPL 式 GPIO_PinRemapConfig；③硬件配置以 .ioc 为准（纯文本可直接读），改引脚/时钟先改 CubeMX 再生成，手改生成区文件会被重新生成覆盖。

27. **同一文件/工程混合编码（UTF-8 与 GBK 混存）**
    症状：某些编辑器中文正常、另一些工具乱码；`python -c "open(p,'rb').read().decode('utf-8')"` 逐段试探才能定位断点。
    案例：Health_watch 工程内 GBK 与 UTF-8 文件并存；本技能 known-pitfalls.md 自己也曾变成「UTF-8 主体 + GBK 尾部」——追加时 encoding 参数与文件实际编码不一致造成。
    规避：全工程统一编码（推荐 UTF-8：Keil → Edit → Configuration → Editor → Encoding）；向已有文件追加前先按 bytes 探测原编码，encoding 参数与之一致；发现混合即用 Python 分段按各自编码 decode 后合并回单一编码重写。

28. **全工程不配 NVIC_PriorityGroupConfig，靠复位默认运行**
    症状：早期功能正常，中断加多后优先级行为与预期不符——所写抢占/子优先级数字的含义与设想错位。
    原因：main 漏配分组，NVIC_Init 按复位默认 PRIGROUP 解释位段划分（与常见的 Group_2 心智不一致）。
    规避：main 最先 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2)`（裸机）或 `Group_4`（RTOS）全工程一次；validator 对「main 所在文件用了 NVIC_Init 却无分组」报 VIOLATION，子文件降 WARNING 提示分组常集中在 main 文件。

29. **接手开源/历史工程的审查套路（Health_watch 实测）**
    复现率最高的三类真问题：①ISR 共享缓冲/计数非 volatile（如 usart1.c 的 RxCounter/Usart1RecBuf）；②fputc 与 I2C 事件轮询忙等无超时；③全工程无 NVIC 分组（见 28）。
    另注意：多套软件 I2C 并存时引脚/时钟逐一核对；printf 调试残留会链接进固件增大体积。
    规避：只读分析先跑 `validator --dir 应用代码目录`（厂商库目录自动跳过），真问题逐条申报；未获允许不改工程文件。
