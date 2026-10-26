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

30. **第三方驱动照抄前必须对照手册审查（HAL_Driver_Lib 实测）**
    实例：Mculover666/HAL_Driver_Lib 的 OLED `OLED_Set_Pos` 低 4 位列地址命令写成 `(x&0x0f)|0x01`——SSD1306 手册为 `0x00|(x&0x0f)`，`|0x01` 强制 bit0=1，偶数列全部偏移为奇数，表现为显示整体错位半列。
    规避：引用第三方驱动先核对每条寻址/命令公式（别默认作者上过板验证）；同时注意国内驱动多为 GBK 编码，入库前转 UTF-8；字模数组核对 `const`（非 const 会把字模吃进 RAM）。

31. **__HAL_LOCK 重入：回调里续接收发函数必返 HAL_BUSY（手册实测）**
    HAL 的 IT/DMA 收发函数入口是「不可等待、失败即返回」的标志锁，完成回调在解锁**之前**执行——在 `HAL_UART_RxCpltCallback()` 里再调 `HAL_UART_Receive_IT()` 是最经典重入；收发同时、提前 return 未解锁同样中招。症状高度可辨识：「轮询发送正常，一加接收中断就挂」。
    规避：每处 HAL 调用检查返回值；通信层用 DMA+空闲中断+环形缓冲；同一句柄同一时刻只从一个上下文访问（Mutex 保护）。详见 hal-freertos-field-guide.md §1。

32. **二值信号量当互斥量用：优先级反转偶发卡死（手册实测）**
    低优先级持锁 → 中优先级抢占 → 高优先级等锁——等待时间由无关任务决定，「逻辑上查不出原因」。FreeRTOS Mutex 自带优先级继承；二值信号量没有。注意：**不存在 configUSE_PRIORITY_INHERITANCE 宏**（优先级继承随 Mutex 内建，configUSE_MUTEXES=1 即可）；Mutex 禁止进 ISR；持锁期间禁 HAL 阻塞函数与 vTaskDelay。

33. **同一外设实例 HAL 与 LL 混用（手册实测）**
    HAL 句柄缓存 State/ErrorCode/Lock，LL 直改寄存器 HAL 不知情——「明明初始化了却 BUSY」或「配置被莫名覆盖」。跨外设混用安全。
    规避：同外设要么全程 HAL，要么一次性 HAL 初始化后彻底交 LL（此后不调它的任何 HAL 函数、不依赖其回调）。
34. **游离重复文件干扰判断（实验九实测）**：工程根目录躺着与 Hardware\ 同名不同内容的 Buzzer.c/lcd.h——是否参与编译以 `.uvprojx` 的文件列表为准（不加组=不编译）；`.h` 的解析是"编译文件所在目录优先于 IncludePath"，故 lcd.c 实际用的是 Hardware\lcd.h，根目录那份是死拷贝。分析/清理工程先看 .uvprojx 列表，别被目录里的文件骗。
35. **#include 大小写不匹配（实验九实测）**：main.c 写 `#include "LCD.h"` 实际文件是 lcd.h——Windows/Keil 不区分大小写能编过，但 git 大小写敏感、Linux/CI 直接炸。多人协作/上库前统一大小写。
36. **源码注释二次编码乱码（实验九实测）**：UTF-8 中文被以 GBK 再保存出现「濠电姷鏁**告慨**鐑藉极」式乱码且不可逆。GBK 工程跨编辑器协作必乱：定死一种编码、保存前看一眼中文注释，乱码源头尽早杀。
37. **头注释引脚漂移（实验九实测）**：main.c 头注释写「PA0=S4」，实现早改成 PC4=S4/PC3=S5——改引脚不同步注释，照注释接线必错。交付前引脚表与代码交叉核对一遍。

38. **NVIC IRQ 通道与外设实例不匹配：中断静默失效（GSM 火灾报警实测）**：Serial.c 全程配 USART1（PA9/PA10 + RXNE 中断使能），NVIC 却写 USART2_IRQn（复制粘贴改一半）——USART1 的中断永远不进，Serial_RxFlag 恒 0。主流程恰好没读接收所以没暴露；一旦想回读 GSM 应答立即卡死。NVIC 通道、IRQHandler 函数名、使能的外设三者必须一致；「中断配置了却从不进」先查通道号。
39. **传感器阈值方向与换算极性不符（GSM 火灾报警实测）**：AD2 用 100-raw*100/4096 反向换算（值大=浓度低）却仍用 > 阈值判断，与 AD1 正向换算同向比较——火焰通道报警方向存疑。反向换算后比较方向必须跟着翻转；阈值用打火机/酒精棉人工标定后写死，温度存 int16_t（uint8_t 丢负温小数）。
40. **外设初始化寄生在别的模块（GSM 火灾报警实测）**：蜂鸣器 PA3 的 GPIO 初始化藏在 AD.c 的 AD_Init() 里——删 ADC 模块蜂鸣器静默失效。初始化与使用同模块收敛；发现"顺路初始化"即拆出独立 init。
41. **报警短信只发一次且 AT 盲等（GSM 火灾报警实测）**：锁存后无重发升级（长火灾只收到一条）、无 GSM 未注网补发；AT 序列靠 Delay_ms(500) 盲等不回读 OK/ERROR——模块掉线/波特率错全发现不了。协议交互必须回读+超时；节流三件套见 gsm-at-sms.md §5。
42. **HAL_UART_IRQHandler 与手写 RXNE 共存是有条件的（CoreScribe 实测）**：串口中断先调 HAL_UART_IRQHandler 再在 USER CODE 手写收发，能共存是因为 HAL 的 RXNE 分支只在 `RxState == BUSY_RX` 时消费 DR——从未调过 HAL_UART_Receive_IT 的工程 HAL 空转，DR 完整留给手写逻辑。哪天有人顺手调一次 Receive_IT，DR 被抢、手写流残缺。同一外设选定一种收发模型后不许再调它的 HAL 收发入口。
43. **裸机协作式死循环直接进 RTOS 任务 = 饿死低优先级（CoreScribe 实测）**：GRBL 主循环 busy-loop 搬进高优先级任务后，UI 任务全靠主循环里插的「运动态每 5ms osDelay(1)」让步点（protocol_background_slice）才活着；而 SLEEP 分支遗留的 `while(!(sys.abort)){...}` 没插让步——一旦进入即完全饿死 UI。移植裸机库先给每个原版忙等 while 过堂：插让步点或换事件/通知。移植全流程见 grbl-port-f407.md §6。
