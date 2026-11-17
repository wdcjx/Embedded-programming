# embed 技能评测集（evals）

10 个标准任务书 + 期望要点。用于度量 **SKILL.md 本身的教学效果**：
任何 Agent（装载 embed 技能）拿到任务书后生成代码，对照期望清单打分。

## 跑法

1. 新开一轮会话，确认 embed 技能已装载（frontmatter 触发）。
2. 原样粘贴任务书（不加额外提示）。
3. 按「期望」逐项核对交付物；validator 结果必须为 PASS（0 VIOLATION）。
4. 记录：触发项 / 部分达成 / 未达成，及生成代码的 validator 输出（--format json）。

评分：每任务 10 分——交付完整性 4 / 纪律正确性 4（validator+关键纪律点）/ 诚实边界 2（如实声明未实测/假设）。

---

## E1 点灯最小闭环（SPL 裸机）

**任务书**：STM32F103C8T6，PC13 板载 LED，非阻塞 500ms 闪烁，串口 PA9/PA10 115200 每秒打印心跳。

**期望**：SysTick 毫秒节拍 millis()（非 HAL_Delay 死等）；主循环非阻塞翻转；fputc 重定向+MicroLIB 提示；引脚分配表+接线表；validator PASS；AFIO 未用不使能（不画蛇添足）。

## E2 按键 EXTI（SPL 裸机）

**任务书**：PA0 按键下降沿触发 EXTI0，翻转 LED；不得阻塞主循环。

**期望**：EXTI0_IRQHandler 标准名；GPIO_EXTILineConfig+AFIO 时钟（R10 关键位）；ISR 只置 volatile 标志、处理归主循环；去抖策略说明（标志+主循环计数或 ISR 内短去抖的取舍）。

## E3 串口不定长帧（SPL 裸机）

**任务书**：USART1 115200，接收不定长帧（\r\n 结尾或 IDLE），收满一帧回显。

**期望**：RXNE+IDLE 双事件方案（idle 检测清 SR 读 DR 序序正确）；缓冲 volatile 或索引 volatile（SPSC 粒度——坑 84）；等待循环带超时（R7）；帧溢出保护。

## E4 PWM 呼吸灯（SPL 裸机）

**任务书**：TIM3 CH1 PA6，1kHz PWM，呼吸灯 2s 周期。

**期望**：PSC/ARR 计算过程（72MHz/72/1000=1kHz）；占空比用 CCR 更新非重配；主循环非阻塞斜坡；MOE 不涉及（通用定时器）但 TIM1 会提。

## E5 ADC+DMA 多通道（SPL 裸机）

**任务书**：ADC1 4 通道（PA0~PA3）DMA 循环采集，1s 打印均值。

**期望**：DMA 循环模式+双缓冲或停下处理说明；ADC 时钟 ≤14MHz（分频说明）；校准（ADC_ResetCalibration/StartCalibration——F1 必做）；均值滤波除数正确（坑 56 家族）。

## E6 FreeRTOS 多任务改写（SPL+RTOS）

**任务书**：把 E1+E2 合并成 FreeRTOS 工程：LED 任务+按键任务，按键通知 LED。

**期望**：无 SysTick_Config（R12）；ISR 用 FromISR+portEND_SWITCHING_ISR（R4）；NVIC_PriorityGroup_4；任务句柄 NULL 守卫（坑 92——技能自身被审查抓过的点）；FreeRTOSConfig 关键项说明。

## E7 I2C 传感器读取（SPL 软 I2C）

**任务书**：软件 I2C PB10/PB11 读 MPU6050 WHO_AM_I，打印校验。

**期望**：开漏/推挽方向控制正确（读改输入）；时序延时来源说明；上拉假设声明；单字节读链（START/地址/寄存器/RESTART/读/STOP）完整。

## E8 内部 Flash 参数保存（SPL 裸机）

**任务书**：保存 3 个参数（u32）掉电不丢，上电读回，按键触发保存。

**期望**：SRAM 影子+整页保存模式（recipes §15）；0xA5A5 魔数三态；**IROM1 缩到参数页起点**（坑 67——必须给出 Keil 修改指令）；擦写限频说明。

## E9 HAL 轨切换（HAL 裸机）

**任务书**：同 E1，但用 HAL（CubeMX 风格）。

**期望**：HAL_Delay vs SysTick 节拍取舍说明；msp 时钟归 MspInit 的结构；CubeMX 生成层 vs 用户层边界声明；HSE 假设（8MHz）声明。

## E10 跨文件引脚冲突（MANIFEST 聚合）

**任务书**：两模块：led.c 用 PA5、beep.c 用 PA5——检查会不会冲突。

**期望**：给出 EMBED MANIFEST 块双文件；跑 validator 多文件聚合报 R14 引脚冲突；提出换脚方案+更新 manifest；解释单文件分析为何看不见（坑 91 盲区）。

---

## 维护

新增能力域（如 CAN/Modbus/BMS）时补 E11+；每条任务书必须可被「未装载技能的 Agent」跑出明显差距（否则评测无区分度）。评测结果记录在 ROADMAP 对应版本节。
