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
44. **控制环塞进 ISR + 全工程 0 volatile：能飞但不规范（四轴遥控/飞控实测）**：飞控把 Fly_task（6 组 float 串级 PID+混控+写 CCR）放 TIM1 ISR 每 4ms 跑，Pitch/Roll/Yaw 等共享变量由主循环写、ISR 读，全工程没有一个 volatile。F103（M3）对齐的 32 位单次读写硬件原子，单个 float 不会撕裂、全局变量跨编译单元每函数重读，加上 LTO 未开——所以"能跑"。但多变量组（姿态三元组、指令+系数）无快照一致性，ISR 里跑软浮点库也拉长关断时间（OLED 软 I2C 刷屏几十 ms 卡主循环正是被迫进 ISR 的根因）。规范做法：ISR 只置标志/拷快照（或双缓冲），PID 放主循环最高优先级时间片；至少给跨上下文变量加 volatile。同类系统完整分析见 quad-drone-spl.md §10。
45. **无线发送死等 IRQ 无超时：最坏阻塞贴着发送节拍（四轴遥控/飞控实测）**：NRF24L01 Send() 里 `while(NRF24L01_R_IRQ()==1)` 裸等，退出全靠 ARC=10 次自动重发耗尽（≈4ms），而发送时间片只有 5ms——链路质量差时发送任务吃满节拍，其余任务集体延后。任何无线/协议发送状态机都必须带超时（定时器计数或看门狗式递减），超时按发送失败处理走重连/降级路径；接收侧已有心跳双窗判定兜底才没出安全事故。
46. **注释集体漂移 + 死码考古：读开源工程以代码为准（四轴遥控/飞控实测）**：同工程成片失实——ISR 节拍注释全部差一（"每3ms飞控算法"实为 4ms，`>N` 清零周期=N+1）、RX_PW_P0=9 注释写"32字节"、SETUP_RETR=0x1A 注释写"580us"（实为 250µs×10 次）、失联保护注释写"自动赋予油门值为0"（实为 Motor_Control 停转）。另有三处死码：遥控 PowerDetection.c（系数还是旧 2S 遗留）、飞控 MyI2C.c 与活代码 MPU6050_I2C.c 同引脚并存、gyro_filter 定义无人调用。教训：接手/沉淀开源工程先跑一遍静态检查建立事实基线，注释只当线索不当依据；死码识别后记录不照抄。

47. **switch-case 缺 break 贯穿家族：同库三处复制扩散（HAL_Driver_Lib 实测）**：W25QXX_ReadSR 的 case 1/2/0 全贯穿到 default——所有调用实际都读 SR1，SR2 永远读不到（QE 位不可用，四线快读使能必错）；lcd_draw_char/lcd_show_char 的 case 12 落 16、case 24 落 32——12/24 号字体实际取 16/32 号字库，窗口按 12/24 算数据按 16/32 发。特征：编译不报错、"能跑"，只有走到特定分支才坏（字号错位、某寄存器读不到）。C 语言 switch 是跳转表不是离散匹配：每个 case 显式写 break/default，代码评审对"连续 case 相邻"的函数逐个核对；第三方驱动入库前对 switch 密集的取值分支做穿透检查。
48. **多组寄存器读入共用小缓冲未及时合成：先读的被后读覆盖（HAL_Driver_Lib HTS221 实测）**：HTS221.c 温度校准读 0x3C/0x3D（T0_out）与 0x3E/0x3F（T1_out）分两次 I2C 读进同一对 buffer[0]/[1]——第一次的结果被第二次覆盖；合成却用 buffer[3]<<8|buffer[2]（从未写入的栈垃圾）→ T0_out=T1 原始值、T1_out=垃圾、插值斜率必错，温度输出面目全非。同函数湿度路径"读完一组立即合成"所以幸免。规矩：每组寄存器读入独立变量并就地合成，不留"先攒后算"；打开编译器未初始化变量警告（KEIL:#174-D/GCC:-Wuninitialized），引用第三方传感器驱动先跑 validator 再上板对已知值（HTS221 手指加热应单调升）。
49. **NVIC 分组设置的位置敏感性：先 NVIC_Init 后换分组=优先级被重新解释（平衡小车实测）**：全工程唯一的 `NVIC_PriorityGroupConfig(NVIC_PriorityGroup_3)` 藏在 BT04_Init() 里，而 main 第一行 uart_init() 已先对 USART1 做了 NVIC_Init(3,3)——分组切换前编码的优先级，硬件按新分组（3 抢占+1 子）重新切位解释，USART1 的字面 (3,3) 从此名不副实。工程能跑纯属巧合（EXTI(0,0) 在任何分组下都最高、两串口间无嵌套需求）。规矩：分组设置放 main 第一条外设初始化之前、全工程只写一次；评审时 grep 所有 `NVIC_PriorityGroupConfig`，出现两处以上（含不同组）直接打回。validator 的 NVIC 规则查"多处不一致"，本例暴露其盲区——只设一次但位置太晚同样违规，顺序需人工核对。
50. **哑中断对：TIM_ITConfig 开了外设中断源、NVIC_Init 缺失，ISR 永不执行（平衡小车实测）**：Encoder.c 对 TIM3/TIM4 都开了溢出更新中断，但两个定时器的 NVIC 通道从未使能——中断标志置位也没人进 ISR，成了"哑火"。更妙的是 TIM4_IRQHandler 内部检查的是 `TIM_GetITStatus(TIM3,...)`（复制粘贴未改），即便 NVIC 开了，TIM4 也会因标志永不清而中断风暴。两个 bug 互相抵消成死代码，而 Read_Speed 每 10ms 清 CNT 让回绕极罕见——多重侥幸叠加才无害。规矩：TIM_ITConfig 与 NVIC_Init 成对出现，不需要中断就两处都不写；ISR 函数体内的外设名与函数名做一次人工比对（复制粘贴重灾区）。
51. **调参旋钮死参数三连：注释宣称可调、实际未被使用（平衡小车实测）**：① 机械中值 Med_Angle 传进 Vertical(Med,Angle,gyro) 但函数体只算 `Kp*Angle+Kd*gyro`，注释还写着"在这里修改你的机械中值即可"；② 转向约束 Turn_Kd 的 if/else 两分支都赋 0，"开/关约束"实际恒关；③ 注释里整组 20ms 参数从未生效。特征：车"能跑"（装配零点凑巧+速度环兜底），调参时旋钮拧了没反应，新手会怀疑自己。规矩：交付前 grep 每个导出调参变量的真实使用点；对"参数→表达式"逐个建立映射；评审任何 PID 模板先确认期望角、中值、死区三类参数真的进了误差项。

52. **采样率注释自欺：CubeMX Prescaler 域是寄存器值不是分频比（DDS 电子琴+FFT 实测）**：TIM3 配 Prescaler=72、Period=180-1，源码注释写「72000000/72/100=10khz」——两处失实：①PSC 寄存器 72 实为 73 分频；②Period 写 180 注释抄 100。实际采样率 72e6/73/180=5479Hz，而 FFT 频段标签全按 10kHz 标注，**整条频段标尺系统性偏移 1.87 倍**（注释 bin2=19.5Hz，实际 10.7Hz）——柱状图当 UI 无碍，当仪器读数全错。规矩：任何时基注释必须以 `f = 时钟/(PSC+1)/(ARR+1)` 用寄存器值现算；CubeMX 界面里 Prescaler/Period/Counter Period 三个域全部是"写入寄存器的值"，Period 界面值=ARR（无需±1），但频率公式里要 +1——域语义最容易混，验算一遍再落注释。

53. **EXTI 回调里重跑 MX_xxx_Init+Start_DMA 全家桶：ISR 里跑 Init 级 API（DDS 电子琴实测）**：每次按键变音高，EXTI 回调整走 `MX_TIM2_Init()`（完整 HAL_TIM_Base_Init+MspInit 判重+全寄存器配置）+`HAL_TIM_Base_Start`+`HAL_DAC_Start_DMA`。能响，但 ISR 上下文跑 Init 级 HAL（时长不可控；一旦有人加日志/延时/嵌套即炸），且 CIRCULAR DMA 每键重启纯属浪费。配套症状：tim.c 里 MX_TIM2_Init 生成体被 #if 0、复制体改读全局 psc 塞进 USER 区——"生成体-影子体"双份漂移。正解三行：`__HAL_TIM_DISABLE → htim.Instance->PSC = 新值-1 → __HAL_TIM_ENABLE`，DMA 通道常开不重启。规矩：ISR 只置标志/改数据寄存器，Init 级 API 一律出中断；变频变频率类需求永远走"停-改-跑"，不走"重初始化"。

54. **CubeMX 允许 ADC ContinuousConvMode 与 ExternalTrigConv 同时勾选：F1 上外部触发优先、Continuous 无效（DDS 电子琴+FFT 实测）**：.ioc 里 ADC1 同时配 ContinuousConvMode=ENABLE 和 ExternalTrigConv=T3_TRGO——CubeMX 不拦，F1 实际以 EXTTRG 为准（TIM3 TRGO 定节拍 5.5kHz），Continuous 位不起作用，读代码的人却会按"连续转换=自由跑满速"误判采样率来源。规矩：选了定时器外部触发就把 Continuous 关掉；读别人工程判断采样率，先看 ExternalTrigConv 再看 Continuous，两个都亮时以触发源为准；生成 ADC 配置时把"触发源→节拍公式"写进注释防误读。

55. **换时钟源不改时基：HSI 64MHz 跑着 72MHz 的定时器（300W 加热台温控器实测）**：为省晶振走 `HSI/2×16=64MHz`（F1 HSI 路径上限 64M，到不了 72M），但全工程分频与注释仍按 72MHz 留下——TIM1 测温节拍注释 1s 实际 1.125s、TIM2 加热 PWM 注释 1s 实际 1.1875s、SPI 注释 18MHz 实际 32MHz。加热台靠热惯性扛住了，复制到任何时序敏感应用（串口波特率/采样率/PID 周期）立即失准。规矩：改时钟源后 grep 所有 `Prescaler|Period|BaudRate|注释里的 MHz`，用新频率逐个重算并同步注释；HSI 方案还要答三问（要不要 72M/USB/±1% 容差够不够，异步串口高波特率慎用）。

56. **去极值均值滤波除数写 N 而非 N−2，读数×(N−2)/N（MAX6675 测温实测，安全级）**：读 5 次、`sum−max−min` 后剩 3 样本之和却 `/readTimes`（5）——温度读数系统性×0.6，显示偏低 40%、闭环设定点实际到不了或永远不可达（满功率加热靠散热兜底）。叠加第二个隐患：热电偶开路返回 0xff=255℃，无故障停机处理，tSet=350 时 error=95 仍继续加热。规矩：去极值滤波分母必须是 N−2，写完用恒温源校一点；传感器开路/超量程值必须映射到停机+报警，绝不能是"看起来正常的数"。

57. **Flash 参数区先擦后写+半条记录校验不全，掉电丢数据（加热台存储实测）**：追加式磨损均衡（顺序写 12B 记录、写满 ErasePage 重来）平时正确，但两个掉电窗口致命——写满时"先整页擦除再写"，窗口内掉电全丢；逐字写一条记录中途掉电留半条，而读取范围校验只查 duty/tSet 两字段，第三个字段 initDelay 读到 0xFFFFFFFF 垃圾。规矩：掉电安全用双扇区交替+序号/valid 标志字（新页写完标 valid 再擦旧页）；读取校验必须覆盖结构体**每一个**字段；页尾参数区要查 .map 确认程序没占。

58. **NRF24L01 多节点同地址同信道还开着 EN_AA：广播拓扑配单播应答（多节点机器人实测）**：五节点（遥控/底盘/云台/灯条/发射）收发地址 5 字节全配同一串、RF_CH 同 40、EN_AA=0x01——一包发出全体收到（广播效果），但 Enhanced ShockBurst 的 ACK 是单播语义：多个接收方同时回 ACK 在空中碰撞，发送方认为丢包触发重发，靠 SETUP_RETR 掩盖。表现为"偶尔丢帧、重发后又能收到"，规模一大（节点再多/干扰环境）重发风暴。规矩：真广播关 EN_AA/重发走纯 PTX/PRX；或每节点独立地址+各自 pipe（广播地址单独开 pipe0）；同地址"伪广播"只在 2~3 节点、容忍丢包的灯效类链路上可用。

59. **HAL MspInit 强符号一处定义只覆盖部分外设：其余时钟无声缺失（发射机构实测，真硬件 bug）**：工程里 motor.c 定义了 `HAL_TIM_PWM_MspInit` 且只处理 TIM8（`if(htim->Instance==TIM8)`）；Biu.c 又对 TIM2/TIM3 调 `HAL_TIM_PWM_Init`——HAL 弱符号被 motor.c 强符号抢占，TIM2/TIM3 的分支不存在，**时钟使能代码永远不会执行**，PWM 无输出（或靠别处巧合初始化）。HAL 的 MspInit 是"全工程每个外设类型一个强符号"，不是"每个 .c 文件各写各的"。规矩：一个工程一个 `HAL_xxx_MspInit`，内部 `if(Instance==TIMx)` 覆盖**所有**用到的实例；新增定时器先往 MspInit 加分支再写 Init；评审时把 MspInit 实例清单与 MX_xxx_Init 调用清单做差集，差集非空即打回。

60. **FreeRTOS 任务模板复制粘贴：三任务共用一个句柄+start_task 删除自己后再 taskEXIT_CRITICAL（多节点机器人实测）**：①my_task.c 里 light/motor/DATA_SEND 三个 xTaskCreate 的句柄参数全是 `&DATA_SEND_Handler`——句柄没人用时无害，一旦查任务状态/删除指定任务就张冠李戴；②正点原子模板 start_task 里 `vTaskDelete(startTaskHandle)` 之后还写着 `taskEXIT_CRITICAL()`——任务删除后代码永不执行，临界区计数随 TCB 销毁被丢弃（调度器恢复运行靠任务删除路径本身，不出事但语义骗人）。规矩：任务句柄一一对应命名；启动任务直接在 main 里 xTaskCreate 完毕 vTaskStartScheduler，不搞"创建者自杀"；模板里 `vTaskDelete(自己)` 后面的代码视为死码删除。

61. **FOC 互补 PWM 不配死区不配刹车：分立半桥直通风险（4310 直驱云台实测快照）**：TIM1 中心对齐三相互补 PWM（25kHz）全工程没有 `HAL_TIMEx_ConfigBreakDeadTime`——TIM_BDTR 的 DTG/MOE 全默认 0。分立 MOS 管+栅极驱动若无硬件死区，上下管同相切换瞬间直通，轻则发热重则炸管。工程当前处于开环低速调试态侥幸未炸，闭环高频换向必踩。规矩：只要输出互补 PWM（FOC/H 桥/无感），死区 0.5~1µs+MOE=1 必配；BDTR 刹车输入（过流信号接 BKIN）是免费保险；HAL 侧就是一行 `HAL_TIMEx_ConfigBreakDeadTime(&htim1,&sBreakDeadTimeConfig)`。

62. **角度单位三进制混用：count/度/弧度在一条数据链上打架（AS5600+FOC 实测）**：`as5600_read_angal()` 返回 raw count（0~4095）且换算角度的行被注释；`Get_Angel_L` 用 `/4096×2π` 出弧度；`Get_Velocity_H` 的回绕判断却按度写（`diff>180 → −360`），而输入是弧度——|diff|≤π<180 永不触发，靠圈数跟踪兜底才没炸；PID 误差又 `×180/PI` 转度、`Get_Angel_Notrack` 再 `0.08789°×π/180` 出弧度。每个函数局部都对，链起来单位乱成一团，速度/加速度类派生量必错。规矩：接口统一 SI（弧度/弧度每秒），count→物理量的转换只在传感器驱动边界做一次；每个函数注释标返回单位；回绕判断与被比较的量必须同单位（π vs 180 一眼假）。

63. **位运算 `|` 与算术 `+` 同行无括号：偏移量 0 掩盖 bug（OV-Watch CST816 实测）**：`CST816_Instance.Y_Pos=((DAT[2]&0x0F)<<8)|DAT[3] + TOUCH_OFFSET_Y;`——C 里 `+` 优先级高于 `|`，实际算成 `(...<<8) | (DAT[3]+OFFSET)`；因为 `TOUCH_OFFSET_Y` 恰好定义为 0，坐标永远正确，一旦有人改成非 0 偏移就悄悄错位。同类高危组合还有 `a<<b+c`、`x&mask==y`（`==` 高于 `&`）。规矩：位运算与算术/比较混用一律显式括号；`#define` 常量为 0 的"加法"本身就是死码味道，review 时单独过一遍。

64. **Boot→APP 双区固件：VTOR 改了 Keil IROM1 没改（或反之），烧录错位/跳转进HardFault（OV-Watch IAP 实测正反两面）**：IAP 双区工程的 APP 侧必须两处同步——main 第一行 `SCB->VTOR = 0x0000C000U;`（向量表重定位，源码注释原话「this must set same as keil setting」）**和** Keil Target→IROM1 起始地址改 0x0800C000、长度扣掉 Boot 区。只改一头：链接器把 APP 链到 0x08000000 覆盖 Boot（变砖），或代码里 VTOR 指向 0xC000 而固件链在 0x0000（跳转即 HardFault）。规矩：IAP 工程交付清单里 VTOR/IROM1/散列地址三处核对；跳转五件套（关 SysTick 三寄存器→__disable_irq→取 APPLICATION+4 入口→__set_MSP→跳转）逐条对照，APP 侧 main 开头配 __enable_irq。

65. **FreeRTOS 进 STOP 三件套缺一不可：漏关 TICKINT 睡不着、漏重配时钟醒来乱（OV-Watch 智能手表实测正解）**：RTOS 下进 STOP 的正确序列=①`vTaskSuspendAll()` 停调度 ②停看狗（外部看狗可 Disnable，IWDG 停不掉所以低功耗产品用外狗+GPIO 喂）③`CLEAR_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk)` 关 tick 中断——否则内核 1ms 一次的 tick 中断持续把 WFI 打醒，STOP 形同虚设；醒来后配对三件=①恢复 TICKINT ②`HAL_SYSTICK_Config(SystemCoreClock/(1000/uwTickFreq))` ③**重跑 `SystemClock_Config()`**（STOP 醒来时钟源回 HSI、PLL 已断，不重配则全系统降频乱套）。醒后还要"查唤醒源再决定干活"（假醒 goto 回睡），否则每次 RTC 周期唤醒都白亮屏。

66. **嵌入式运行期 malloc/free 做订阅链表：碎片+非线程安全（OV-Watch pubsub 实测）**：pubsub.c 的 Publisher_subscribe/unsubscribe 用 `malloc(sizeof(SubscriberNode))`/`free` 维护链表——桌面范式直接搬进固件：heap_4 下碎片可控但订阅/退订频繁时内存形态不可预测，且 malloc 与 ISR/多任务并发无保护（本工程订阅集中在初始化期、板上实际只用静态一个 publisher，侥幸无害）。规矩：订阅表用静态数组+计数（嵌入式事件总线标准做法）；确需动态内存用 pvPortMalloc/vPortFree 且只在任务上下文；初始化完成后禁止再 malloc 的纪律由 validator 强制。

67. **自定义 Flash 参数页选在末页但 Keil IROM1 没缩：代码长进参数页无警告静默覆盖（江协 15-1 实测隐患）**：工程把参数存 0x0800FC00（C8T6 20KB Flash 末页），但 uvprojx 里 IROM(0x08000000, **0x10000**) 声明 64KB 全量——链接器不知道参数页存在，代码一旦超过 19KB 就会链进参数页地址段，烧录后覆盖参数、程序读回错误数据，全程无任何警告（教学工程代码 ~5KB 侥幸无害）。规矩：凡是自定义 Flash 存储区，交付清单必含「IROM1 长度缩到存储区起点」（C8T6 末页方案=0x4C00）；生成参数保存代码时同步给出 IROM 修改指令；更稳妥做法是加链接器检查（`--keep`/map 文件核对最高地址）。

68. **串口 ISR 里调刷屏函数：软 I2C OLED 全屏刷新几十 ms 级，跨文件调用链 validator 也看不见（桌面宠物 A 版实测，B 版官方整改对照）**：A 版 USART1/USART3 的 RXNE 中断里命令解析后直接调 `Face_Config()`→`OLED_Clear+ShowImage+Update`——软 I2C 全屏 1KB 实测几十 ms 起步，整个跑在中断里：期间另一串口数据（DR 单字节无 FIFO）必丢、TIM 低优先级任务全部堆积。validator 单文件分析只见 ISR→extern 函数调用，看不见 OLED_Update 在调用链尾端，静态检查盲区。**同作者 B 版已整改**：ISR 只存 `RxData_Blue`+调纯变量赋值的命令表函数，刷屏归主循环——这就是"ISR 只置标志"的标准整改路径。规矩：ISR 内允许调的函数=只写共享变量的；任何会走到 I2C/SPI 传输、Delay、长循环的函数一律主循环消费标志；跨文件调用链是静态检查天然盲区，评审时人工追 ISR 的完整调用栈。

69. **多来源同协议的命令表复制 N 份维护：贯穿 bug 也复制 N 份（桌面宠物 0x39 实测）**：A 版语音/蓝牙两个串口 ISR 各复制一份 190 行 if-else 命令表；B 版统一成 `PetAction_Judgmengt(Serial_type)` 但函数体内仍是 switch(语音)/switch(蓝牙) 两份复制——结果 case 0x39（摇摆提速）缺 break 贯穿到 0x40（摇尾巴开关）的 bug 在两份里一模一样：调提速会顺带翻转尾巴开关。复制粘贴 bug 会随复制体扩散（坑 47 家族第三例，且是"复制的复制"）。规矩：**同协议多来源只写一份命令表**（入口函数参数区分来源，来源差异点仅限个别标志位如本例 Sustainedmove）；复制出的第二份永远要 diff 校对每个 case 的 break；命令表用 switch 时收尾加 default:break 兜底。
