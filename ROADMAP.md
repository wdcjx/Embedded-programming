# ROADMAP — embed 技能演进计划

> 使用中持续完善。完成打 [x]，进行中打 [~]，未开始留 [ ]。

## v0.6.2 F4 跨家族起步（F407VET6 工程实测）

- 来源：D:\STM32CubeMX\text3（F407VET6 LQFP100，CubeMX + MDK-ARM，168MHz 骨架：UART4 PA0/PA1、TIM2 CH1、按键/LED 引脚；业务代码待写）
- 新增 references/f4-family-notes.md：时钟树 168MHz 实测模板（M=8/N=336/P=2/Q，VCO 输入 1MHz 检查锚）、F1→F4 差异对照表（TIM2/TIM5 32 位、FLASH_LATENCY_5、PWR Scale1、APB1 42/Tim84、FPU、GPIO 速度档语义）、换轨清单（不变的 CubeMX 布局/USER CODE/回调纪律）、text3 引脚快照、F4 待办（上板实测/PWM 配方/printf 重定向/F4+FreeRTOS/FPU 选项）
- 状态：工程文件级实测 + validator 三文件 0/0（F4 零误报）；未上板未编译——运行时坑待补
- SKILL.md 能力行同步：F4 从「按 HAL 轨生成并声明未实测」升级为「有起步笔记基准」；F7/H7/G0/G4/L4 仍按未实测声明

## v0.6.1 波形发生器与 TFT-LCD（实验九工程实测）

- 来源：D:\stm32project\嵌入式实验九2（F103 大容量 HD + FSMC ILI9341 240×320 + DAC/ADC 回环波形实验，"Nemo M3S" 板）
- 新增 references/fsmc-lcd-ili9341.md：FSMC 地址译码原理（NE→Bank 基址、RS 接 A10→bit11）、引脚全表、SPL 初始化模板（时序 0x0F/0x0F 宁慢勿快）、All-4-NE 兼容探测技巧、ILI9341/9328 双驱动与 0xD3 读 ID（先空读）、窗口绘图 API 约定、RGB565 R/B 硬件交换、坑 6 条
- peripheral-recipes.md 新增 §12 DAC 配方（HD/XL，无触发直写+回环自测）与 §13 定时器节拍波形发生器+示波器信号链架构（ISR 产数据/LCD 只在主循环刷/环形缓冲降采样/状态机集中改参/`_Callback` 转发命名）
- stm32-stdperiph-api.md 新增 §11 DAC、§12 FSMC 速查段
- known-pitfalls.md 新增坑 34（游离重复文件以 .uvprojx 为准）/35（#include 大小写）/36（注释二次编码乱码）/37（头注释引脚漂移）
- validate_generated.py：`*_Callback` 后缀豁免向量名校验（it.c 转发模式误报根除；中断纪律仍由 is_isr_context 的 Callback 分支覆盖）；回归 6 自测样本不变，实验九 main.c 1 误报清零（余 3 条忙等 WARNING 为真实提示）
- 新能力声明：DAC、FSMC+TFT（HD/XL）、波形发生器信号链架构

## v0.6 现场可靠性沉淀（避坑手册消化）

- 来源：用户整理《STM32实战避坑手册_HAL与FreeRTOS》v1.0（F1/F4/H7、HAL v1.12–1.18、FreeRTOS V7.5+，15 类高频故障）
- 新增 references/hal-freertos-field-guide.md（13 节增量，与既有文档去重后收录）：__HAL_LOCK 全解（回调续接重入/HAL_BUSY 检查纪律）、阻塞收发时间成本表、HAL vs SPL 开销实测与同外设 HAL/LL 混用禁止、启动三处隐性卡死（uwTick 排查第一原则）、临界区分层（禁 __disable_irq）、Mutex 优先级继承 vs 二值信号量（configUSE_PRIORITY_INHERITANCE 不存在）、任务栈 64B 固定开销与高水位流程、堆预算清单、落地顺序六步（先立规矩再跑业务）、D-Cache+DMA（F7/H7 预埋）、低功耗坑（待机电流 75 倍案例）、FreeRTOSConfig 最小配置集+configASSERT、常见误解对照表
- 新增 references/symptom-quickref.md：14 条症状→根因反查表（映射到技能内文档）+ 三条排查原则——补上「从现象出发」的检索维度
- known-pitfalls.md 追加坑 31（__HAL_LOCK 重入）/32（二值信号量互斥→优先级反转）/33（同外设 HAL/LL 混用）
- SKILL.md 工作流加步骤 0（故障排查路由：死机/挂死类问题先查症状速查表）；资源表、README、能力行同步
- 剩余待办：HAL 外设配方（UART DMA/TIM PWM/ADC DMA 的 CubeMX 配置细节）、F4 跨家族扩展（待硬件落实）

## v0.5.4 HAL+FreeRTOS 完整集成（text2 实测，四轨集齐）

- 实测工程：D:\STM32CubeMX\text2（F103C8T6 + FW_F1 V1.8.7 + CMSIS_V1 + 时基 TIM2）；validator 四文件（main/freertos/timebase_tim/it.c）0 违规 0 警告——混合轨零误报
- 新增 references/freertos-hal-integration.md：CubeMX 配置路径、生成文件地图（时基回调在 main.c USER CODE、HAL_InitTick 重写在 timebase_tim.c、任务创建 V1 在 main.c）、tick 数据流、三宏与时基选择联动（SysTick 宏注释机制）、CMSIS V1↔V2 API 对照、RTOS heap(3072) 与 C 库堆(0x200) 双堆模型、任务/中断纪律、手写集成清单 7 步
- ROADMAP 待办「HAL + FreeRTOS 完整集成文档」完成；四轨（裸机 SPL/SPL+RTOS/裸机 HAL/HAL+RTOS）全部有实测基准
- 剩余待办：HAL 外设配方（UART DMA/TIM PWM/ADC DMA 的 CubeMX 配置细节）、F4 跨家族扩展（用户意向待硬件落实）

## v0.5.3 HAL 第三方驱动范式（HAL_Driver_Lib 基准）

- 新增 references/hal-driver-patterns.md：以 Mculover666/HAL_Driver_Lib（BSD-3，本机 D:\stm32project\HAL_Driver_Lib）18 驱动为基准，提炼 HAL 传感器/存储驱动通用写法——三层分离（static 移植层/协议层/API 层）、句柄注入两式、I2C 命令型与存储型时序、SPI+CS 接口收敛、DWT 微秒延时与 HAL_InitTick 时基迁移（裸机释放 SysTick / RTOS 双时基无冲突）、OLED 与江协版对照、命名流派与错误码约定、生成检查单 7 条
- 发现并沉淀第三方驱动真 bug：OLED_Set_Pos 列低地址 `|0x01` 应为 `|0x00`（坑 30）——引用第三方驱动必须对照手册审查
- 本地参考库：D:\stm32project\HAL_Driver_Lib（生成 HAL 驱动代码时可对照其写法）

## v0.5.2 OLED（SSD1306）驱动沉淀

- [x] 用户要求沉淀 OLED 代码；定位基准=江协科技例程（本机 230 个 OLED 文件溯源：4 针 I2C 版为正本，SPI 版 7940B 为变体；Health_watch OLED_I2C.c 与智慧农业 oled.c 为另两套实现，仅记录家族不混编）
- [x] references/oled-ssd1306.md：引脚表（软件 I2C PB8/PB9 开漏 OD、SPI 五线 PB12~15+PA8 推挽）、显存模型（8 页×128 列、字节竖排 D0 在上、8x16 字符=2 页 8 列→4 行×16 列）、I2C 协议（写地址 0x78、控制字节 0x00/0x40、免 ACK 原理）、SPI 五线时序（DC 命令/数据、RES 复位脉冲）、初始化命令表（充电泵 0x8D=0x14 最常见不亮根因）、江协风格 API 约定（8 函数签名、Length 前导零）、移植清单（换脚/HAL 对照/硬件 I2C/RTOS 阻塞刷屏 40~80ms 禁 ISR）、坑 7 条（含字模必须 const 否则 1.5KB 白吃 RAM）
- [x] SKILL.md 资源表、README 文件树/能力行同步

## v0.5.1 开源工程审查回归（Health_watch SPL 传感器工程）

- [x] validator 修复 3 处：①NVIC 分组检查分级——main 所在文件用了 NVIC_Init 却无分组=VIOLATION，子文件降 WARNING（提示分组常集中在 main 文件）；②时钟顺序检查改「函数内比较」——方向切换等辅助函数排在 init 前导致的文件级误报根除（Health_watch iic.c/ds1302.c 教训）；③厂商库目录自动跳过（SKIP_DIR_PARTS 含 Libraries/StdPeriph/CMSIS/HAL_Driver/Middlewares，输出跳过计数）——智慧农业 SPL 库源码 8 条误报根除
- [x] 回归：六自测样本不变（10/4/4 违规 FAIL，三模板 PASS）；智慧农业 22 应用文件 0 违规 7 警告 PASS（跳过 25 库文件）；Health_watch User 16 文件 2 违规 9 警告 FAIL——真发现=usart1.c:97 ISR 访问非 volatile RxCounter/Usart1RecBuf ×2，时钟/NVIC 误报清零
- [x] known-pitfalls.md 追加 27~29：混合编码（UTF-8 与 GBK 同工程/同文件混存，含本技能自身文件踩坑案例）、全工程未配 NVIC 分组（靠复位默认运行）、接手开源工程审查套路（validator --dir 全扫 + 三类高发真问题）
- [x] known-pitfalls.md 编码修复：UTF-8 主体 + GBK 尾部（历史追加 encoding 不一致造成）→ 统一 UTF-8；追加前一律先探测原编码
- [x] peripheral-recipes.md §11 传感器/总线驱动模式小抄：软件 I2C 骨架（五函数+方向切换）、DS18B20 单总线时序与关中断、DS1302 三线 BCD/WP、OLED 硬件 I2C1、第三方驱动接入检查点

## v0.5 HAL 轨起步（进行中）

- [x] 用户环境确认：F103C8T6 不变；CubeMX（MDK-ARM）生成工程 + Keil 编程；HAL 固件包版本未提供，按版本区间沉淀、以实际编译为准
- [x] references/hal-basics.md：时钟锚定、CubeMX 要点、USER CODE 区纪律、SPL→HAL 句柄模型对照、回调=中断上下文范式、validator HAL 规则、F1 起步坑 6 条、HAL+FreeRTOS 两条铁律
- [x] templates/main_hal.c：裸机 HAL 骨架（PC13 + USART1，MspInit 集中时钟/引脚，CubeMX 结构对照 + 手写起点），validate 0 违规 0 警告 PASS
- [x] validate_generated.py HAL 支持：HAL 工程识别、`__HAL_RCC_xxx_CLK_ENABLE` 时钟宏、回调按 ISR 纪律检查（含 HAL_Delay→VIOLATION）、Error_Handler 豁免、外设初始化模式兼容 HAL_ 前缀、RTOS+HAL 时基确认提示
- [x] tests/bad_sample_hal.c：4 违规 FAIL；六样本回归全过（10/4/4 违规，三模板 PASS）
- [x] SKILL.md 升级为四轨路由（裸机 SPL / SPL+FreeRTOS / 裸机 HAL / HAL+FreeRTOS）
- [ ] HAL 外设配方（HAL 版 recipes：UART IT/DMA 收发、TIM PWM、ADC 校准+DMA、I2C Mem 读写、SPI+CS）
- [ ] HAL + FreeRTOS 完整集成文档（CubeMX FreeRTOS 中间件、时基 TIM 细则、osDelay 范式、heap/栈预算）
- [x] HAL 真实工程回归：text1 最小工程（CubeMX 6.18.1 / FW_F1 V1.8.7 / MDK-ARM V5.32，F103C8T6，GPIO+SWD，已编译）——validator 三文件 0 违规 0 警告 PASS；实测沉淀：.ioc 配置读法、启动文件 startup_stm32f103xb.s 命名、MspInit 集中在 stm32f1xx_hal_msp.c、函数内 USER CODE 区（hal-basics §2/§3 + 坑 25/26）

## v0.4.1 真实工程回归（智慧农业 F103C8 SPL）

- [x] 用户真实工程（44 文件 Keil 工程，SPL + 多模块）全量跑 validate_generated.py：修复前 13 违规/11 警告 → 定性为 3 类误报 + 若干真发现
- [x] 检查器修复 4 处：①时钟检查容忍宏封装（全文件无任何 RCC_...PeriphClockCmd 才报 VIOLATION，AFIO 缺字样降为 WARNING）；②`extern` 声明不再按本文件非 volatile 全局上报；③Fault Handler 的常量 `while(1)` 死循环豁免；④带参 `xxHandler(USART_TypeDef*)` 普通函数不再误判为中断向量（is_isr 增加 params 判定）
- [x] 回归：四自测样本结果不变（坏样本仍 FAIL、模板仍 PASS）；用户工程 0 违规/7 警告 PASS，剩余警告均为真实纪律提示（等待无超时）
- [x] known-pitfalls.md 追加 17~24 条（GBK 编码、vsprintf 无界、__align(8) 专属、共享结构体撕裂、Handler 命名误认、ISR 业务函数+清标志顺序、HAL shim、DHT11 采样间隔）
- [x] examples/blink_uart_demo：用户验证"能成功运行"（未上板演示）；状态=编译运行通过，上板验证待定

## v0.4 SPL + FreeRTOS 组合

- [x] references/freertos-spl-integration.md：用户侧内核文件清单与放置、向量接管三宏、Group_4/MAX_SYSCALL 优先级红线、FromISR 范式、20K RAM 内存预算、与裸机模板差异清单、RTOS 特有坑 8 条
- [x] templates/FreeRTOSConfig.h：F103 中密度配置模板（Kernel 10.x~11.x，heap_4 ≤10K，栈溢出/堆耗尽钩子必开，断言宏）
- [x] templates/main_rtos_spl.c：任务工程骨架（建任务+调度器、EXTI→任务通知 FromISR 范式、三个钩子、ISR 抢占优先级 ≥5 示范）
- [x] validate_generated.py 新增 RTOS 检查：ISR 调任务级 API（VIOLATION，附 FromISR 替代建议）、RTOS 工程 SysTick_Config（VIOLATION）、非 Group_4（WARNING）
- [x] tests/bad_sample_rtos.c：RTOS 违规自测样本；四文件回归全通过（RTOS 坏样本 FAIL / RTOS 模板 PASS / 旧样本不受影响）
- [ ] 下一站 v0.5：HAL 基础沉淀 → HAL + FreeRTOS（HAL 时基迁移到 TIM、任务内禁 HAL_Delay 两条铁律）

## v0.3 合并为单技能

- [x] 应用户要求，撤销双技能拆分：删除 `stm32-spl-codegen/` 子目录，纪律文件并入根目录（references / scripts / assets / tests）
- [x] SKILL.md 重写为"模式路由"式：快速模式（简单示例）与纪律模式（默认，validate 清零 + 坑申报 + 完整交付物）共用一套工作流与资源
- [x] 安装回归简单：一个目录 = 一个技能（name: embed）
- [x] 合并后回归测试：validate_generated.py 对 tests/bad_sample.c 报 10 项违规（FAIL），对 templates/main.c 通过（PASS）

## v0.2 阶段二：stm32-spl-codegen（工程纪律技能，独立子目录）

- [x] SKILL.md：角色定位、5 步工作流、7 条硬约束、10 项自检清单、输出格式要求
- [x] references/spl-peripheral-checklist.md：10 类外设初始化顺序清单与强制项
- [x] references/isr-and-concurrency-rules.md：ISR 标志位模式、volatile 规则、CM3 原子性与 PRIMASK 临界区、环形缓冲所有权、标志清除顺序
- [x] references/known-pitfalls.md：16 条已知坑（症状/原因/修法）
- [x] scripts/validate_generated.py：启发式静态检查（时钟先开、volatile、ISR 阻塞、等待超时、动态内存、NVIC 分组、向量名校验、重复定义；violation/warning 分级 + 退出码），已用 bad_sample 与阶段一模板双向自测
- [x] assets/spl-project-template.md：Keil 工程组织、必配项、conf.h 模板、Flash/RAM 估算、bring-up 流程
- [x] tests/bad_sample.c：故意违规样本（自测检查脚本）
- [ ] 实战打磨：随使用补充检查规则与坑位条目

## v0.1.1 当前版本

- [x] SKILL.md 工作流程 + 代码规范 + 自检清单（标准库基准）
- [x] **默认库切换为 STM32 标准外设库（StdPeriph/SPL）**；标注库支持边界（新系列无标准库）
- [x] 标准库 API 速查（GPIO/EXTI/USART/TIM/ADC/I2C/SPI/IWDG）+ 密度宏/启动文件/工程文件清单
- [x] 典型配方：点灯、按键中断、串口回显、RXNE+IDLE 不定长帧、PWM 呼吸灯、ADC 采样、I2C 读取、SPI+CS、看门狗
- [x] F103C8T6 标准库 main.c 工程骨架模板（SysTick 节拍 + 非阻塞主循环）
- [x] VS Code 单独查看模板时 #include 报错的说明（README 常见问题 + 模板头注释 + SKILL 交付物提示）

## 计划中（按使用频率优先补充）

- [ ] 常用模块驱动库（标准库版）：0.96" OLED(SSD1306)、DHT11、DS18B20、AT24C02、ESP8266 AT 指令
- [ ] 软件模拟 I2C 配方（F1 硬件 I2C 时序坑的替代方案）
- [ ] CAN、USB(CDC 虚拟串口)、RTC + BKP、低功耗(Sleep/Stop/Standby) 配方
- [ ] DMA 配方（USART DMA 发送 / ADC DMA 多通道扫描）
- [ ] 多文件工程生成（按 Keil 工程结构输出 main.c / bsp_xxx.c / xxx.h / stm32f10x_it.c）
- [ ] 编译验证：检测本机 arm-none-eabi-gcc / Keil，可用时自动编译检查
- [ ] 可选 HAL 支持（用户明确要求 HAL 工程时启用，独立 references 目录）
- [ ] 其他系列支持（F0 标准库差异、F2/F4 旧版标准库）
- [x] 简单 RTOS 模板：SPL+FreeRTOS 轨已完成（见 v0.4）；HAL+FreeRTOS 待 HAL 基础沉淀后做
- [ ] 常见坑列表持续追加（已有：JTAG 引脚释放、F1 ADC 校准、TIM1 MOE、APB1 36MHz 限制、HSE_VALUE 必须匹配晶振、RTOS SysTick 接管、ISR 非 FromISR API）
