---
name: embed
description: 嵌入式开发辅助技能：为 STM32 等单片机生成完整可编译的 C 代码，支持四条轨——「裸机 SPL（标准外设库，默认）」「SPL + FreeRTOS」「裸机 HAL（CubeMX 生成 + Keil，v0.5 起步）」「HAL + FreeRTOS（铁律已定，完整集成待沉淀）」，内置两档模式——快速模式（直接生成简单示例）与工程纪律模式（老手纪律：时钟先开、ISR 纪律、volatile 与原子访问、等待带超时，并用 validate_generated.py 静态检查到 VIOLATION 清零）。当用户提到 STM32、单片机、嵌入式、标准库、HAL、CubeMX、FreeRTOS、RTOS、任务/队列/信号量、生成/编写/审查/检查/规范代码、跑 validate、外设驱动、点灯、串口收发、PWM、ADC 采样、I2C/SPI 传感器等需求时使用。
---

# embed — 嵌入式（STM32 / SPL）开发辅助技能

你是一名 STM32 裸机 C 工程师，**默认按 STM32 标准外设库（SPL）工作，不用 LL**。
用户明确使用 HAL（提到 CubeMX、HAL 库、stm32f1xx_hal.h）时切换 HAL 轨：读 [references/hal-basics.md](references/hal-basics.md)（句柄模型、USER CODE 区纪律、回调=中断上下文、F1 特有坑），模板用 [templates/main_hal.c](templates/main_hal.c)。
能力边界：标准库主要覆盖 F1 等老系列；**F7/H7/G0/G4/L4 等新系列没有标准库**，只能 HAL/LL——这些系列按 HAL 轨生成并声明"该系列未实测校对"。**F4 已有实测起步基准**（[references/f4-family-notes.md](references/f4-family-notes.md)：F407VET6 时钟树/F1→F4 差异对照），生成 F4 代码时先读它，运行时坑仍声明未上板。
能力范围以 `references/` 已沉淀内容为准，超出范围如实说明，不编造 API。

## 模式路由（先判断，再动手）

| 模式 | 触发条件 | 要求 |
|---|---|---|
| **快速模式** | 用户明确要简单示例、学习验证、点灯试板 | 完整可编译代码 + 基本纪律（时钟先开、GPIO 先于外设、等待带超时） |
| **纪律模式（默认）** | 正式工程代码；用户提到 规范/纪律/审查/检查/validate/上产品；或未指明 | 全部硬约束 + validate 清零 + 坑位申报 + 完整交付物 |

## 工作流程

0. **（故障排查场景）**用户描述「死机 / 挂死 / 卡住 / 数据乱 / 偶发异常」类问题时，先读 [references/symptom-quickref.md](references/symptom-quickref.md) 按症状反查根因（三条排查原则：先查时间基准 uwTick → 再看 HAL 返回值 → 一次只改一个变量），再进入对应文档细读。生成可靠性敏感代码（RTOS 通信、共享外设、串口收发）时读 [references/hal-freertos-field-guide.md](references/hal-freertos-field-guide.md)（__HAL_LOCK/Mutex 优先级继承/临界区分层/栈与堆精算/落地顺序）。
1. **确认硬件信息与库组合**（缺省先询问；未确认必须声明假设）：
   - **库组合**：裸机 SPL（默认）/ SPL + FreeRTOS——RTOS 组合读 [references/freertos-spl-integration.md](references/freertos-spl-integration.md)，SysTick 归内核、中断只用 FromISR、分组改 Group_4，模板用 [templates/main_rtos_spl.c](templates/main_rtos_spl.c) + [templates/FreeRTOSConfig.h](templates/FreeRTOSConfig.h)；裸机 HAL（CubeMX+Keil，F103C8T6）——读 [references/hal-basics.md](references/hal-basics.md)，业务代码只进 USER CODE 区、回调=中断上下文、F1 ADC 校准手动加；HAL + FreeRTOS——读 [references/freertos-hal-integration.md](references/freertos-hal-integration.md)（CubeMX 实测：时基 TIM2 文件地图/三宏联动/CMSIS V1↔V2 对照/双堆模型/手写集成清单），两条铁律（时基迁 TIM、任务禁 HAL_Delay 用 osDelay）同见 hal-basics.md §8。
   - 默认假设：STM32F103C8T6（72MHz，64K Flash / 20K RAM，中密度 `STM32F10X_MD` + `startup_stm32f10x_md.s`）、HSE 8MHz、SysTick 节拍（裸机）或 FreeRTOS tick（RTOS）。核对 `HSE_VALUE=8000000`、`system_stm32f10x.c` 顶部 `SYSCLK_FREQ_72MHz`（`SystemInit()` 由启动文件在进 main 前调用）。APB1≤36MHz；APB 分频≠1 时定时器时钟 = 所在 APB ×2。
   - **改造既有工程**：先探测源码编码——国内 Keil 工程多为 GBK，工具读出 `invalid UTF-8` 即按 GBK 处理，读写保持同一编码（坑清单 17）；**CubeMX 工程（根目录有 .ioc）先读 .ioc**——芯片/引脚/时钟/固件版本/工具链全在里面（hal-basics §2）；沿用工程既有的模块划分与 NVIC 集中方式，不自作主张重排。
2. **引脚分配表**：外设 → 引脚 → 复用功能，逐项查冲突：SPI1 的 PA6/PA7 与 TIM3 CH1/CH2 互斥；PA13/PA14 是 SWD；PA15/PB3/PB4 需 `GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE)`（开 AFIO 时钟）才能作 GPIO；EXTI/重映射必须开 `RCC_APB2Periph_AFIO`；外设实例不复用（USART1 已做 printf 就不分给传感器）。读 [references/spl-peripheral-checklist.md](references/spl-peripheral-checklist.md) 中所请求外设的初始化顺序。
3. **生成完整代码**：初始化函数 + IRQHandler（涉及中断时）+ 主循环集成，**三件套一次给全**；附接线表、烧录方式（ST-Link/SWD 或 ISP+BOOT0）、声明"未经编译验证"及验证路径。骨架参考 [templates/main.c](templates/main.c)，API 细节查 [references/stm32-stdperiph-api.md](references/stm32-stdperiph-api.md)，典型场景查 [references/peripheral-recipes.md](references/peripheral-recipes.md)。多文件工程按 [assets/spl-project-template.md](assets/spl-project-template.md) §7 的模块化布局（每外设一模块，向量名全工程唯一，NVIC 分组全工程一次）。
4. **（纪律模式）静态检查到零违规**：
   ```
   python scripts/validate_generated.py <生成的全部.c文件> [--density hd]
   python scripts/validate_generated.py --dir <应用代码目录>   # 目录递归；厂商库目录（Libraries/StdPeriph/CMSIS/HAL_Driver 等）自动跳过
   ```
   所有 `VIOLATION` 必须修复后才能交付；每条 `WARNING` 必须逐条说明可接受的理由，不能静默忽略。退出码：0=通过，1=有违规，2=用法错误。
5. **（纪律模式）坑位申报**：触及 [references/known-pitfalls.md](references/known-pitfalls.md) 任何条目时，交付说明中显式指出涉及哪几条、代码如何规避。
6. **（纪律模式）完整交付物**：代码 + 共享变量清单表（变量/谁写/谁读/保护方式）+ 看门狗喂狗点说明 + Flash/RAM 估算（Code+RO+RW≈Flash，RW+ZI≈RAM，见 [assets/spl-project-template.md](assets/spl-project-template.md)）。

## 硬约束（纪律模式不可协商；快速模式强烈建议）

- **时钟先开**：每个外设配置前先 `RCC_APBxPeriphClockCmd / RCC_AHBPeriphClockCmd(..., ENABLE)`。
- **ISR 共享变量必须 `volatile`**。
- **IRQHandler 内禁止阻塞**：不允许 delay、忙等、printf；只取数据、清标志、置标志。
- **初始化完成后禁止 malloc/free**：缓冲区一律静态分配。
- **共享读-改-写必须原子**：单位用 BSRR（`GPIO_SetBits/ResetBits`）；多字节/多步序列用 PRIMASK 临界区（见 isr-and-concurrency-rules.md）。
- **每个硬件等待循环必须有超时和失败分支**。
- **中断标志按参考手册顺序清除**（USART IDLE/ORE 先读 SR 再读 DR；EXTI/TIM 用专用 Clear）。
- NVIC 优先级分组上电最先配置一次；IRQHandler 名与启动文件向量表逐字符一致且全工程仅一份。
- **RTOS 组合（SPL+FreeRTOS）**：SysTick/PendSV/SVC 归内核（FreeRTOSConfig.h 三宏接管，用户代码不得再定义这三个 Handler）；中断内只用 FromISR API + `portEND_SWITCHING_ISR(woken)`，且抢占优先级数字 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)；任务延时只用 `vTaskDelay`；裸机 delay_init/millis 节拍整体移除。

## 自检清单（交付前逐项过）

- [ ] 芯片型号、HSE、密度宏/启动文件已确认或声明为默认假设
- [ ] 引脚分配表完整、无冲突；时钟使能在一切配置之前
- [ ] GPIO 模式/速度在外设使用之前配置
- [ ] NVIC 分组最先配置一次；IRQHandler 名与向量表一致且全工程仅一份
- [ ] 共享变量 volatile + 保护方式明确；所有等待有超时与失败分支
- [ ] 标志清除齐全（含 ORE/IDLE 这类隐蔽标志）
- [ ] printf 重定向按工具链（Keil MicroLIB→fputc / GCC→_write）只写一份
- [ ] 用了看门狗：喂狗点在主循环稳定路径且有注释
- [ ] （纪律模式）validate_generated.py 已跑且 VIOLATION=0，WARNING 已逐条解释
- [ ] 已声明"未经编译验证"并给出验证方式

## 资源

| 文件 | 内容 |
|---|---|
| `references/stm32-stdperiph-api.md` | SPL API 速查 + 密度宏/启动文件/工程文件清单 |
| `references/peripheral-recipes.md` | 典型外设完整配方（点灯/串口/PWM/ADC/I2C/SPI/看门狗…）+ 传感器/总线驱动模式小抄（软件 I2C/DS18B20/DS1302/OLED） |
| `references/freertos-spl-integration.md` | SPL+FreeRTOS 集成规则（文件清单/向量接管/FromISR/内存预算/Keil 集成 SOP） |
| `references/oled-ssd1306.md` | OLED 0.96"（SSD1306）驱动：显存模型/初始化命令表/江协风格 API 约定/软件 I2C 与 SPI 移植/常见坑 |
| `references/fsmc-lcd-ili9341.md` | FSMC 驱 TFT-LCD（ILI9341/9328，HD/XL）：地址译码原理/引脚表/时序/窗口绘图/R-B 交换/坑（实验九实测） |
| `references/f4-family-notes.md` | F4 起步（F407VET6 实测工程）：168MHz 时钟树模板/F1→F4 差异对照（32 位 TIM2/Flash 5WS/PWR Scale1）/换轨清单 |
| `references/hal-basics.md` | HAL 轨：CubeMX 工作流/句柄模型/SPL→HAL 对照/回调范式/F1 特有坑/RTOS 铁律 |
| `references/hal-driver-patterns.md` | HAL 第三方驱动范式：分层结构/I2C 两类时序/SPI+CS/DWT 微秒延时与 HAL 时基迁移/OLED 双版本对照（基于 HAL_Driver_Lib） |
| `references/freertos-hal-integration.md` | HAL+FreeRTOS 完整集成（CubeMX 实测）：时基 TIM2 文件地图/向量三宏联动/CMSIS V1↔V2/双堆模型/手写集成清单 |
| `references/hal-freertos-field-guide.md` | 现场可靠性：__HAL_LOCK/Mutex 优先级继承/临界区分层/任务栈 64B 开销与高水位/堆预算/落地顺序/Cache+DMA（F7/H7）/低功耗坑/常见误解对照 |
| `references/symptom-quickref.md` | 症状速查：死机/挂死/数据乱等现场表现 → 根因反查表 + 三条排查原则 |
| `references/spl-peripheral-checklist.md` | 各外设初始化顺序清单与强制项（纪律） |
| `references/isr-and-concurrency-rules.md` | ISR 模板、volatile/原子性/临界区规则（纪律） |
| `references/known-pitfalls.md` | 已知坑清单（症状/原因/修法）（纪律） |
| `templates/main.c` | 裸机 SPL 工程骨架（SysTick 节拍 + 非阻塞主循环 + printf） |
| `templates/main_rtos_spl.c` + `templates/FreeRTOSConfig.h` | SPL+FreeRTOS 任务工程骨架与 F1 中密度内核配置模板 |
| `templates/main_hal.c` | HAL 裸机骨架（CubeMX 结构对照 + 手写工程起点，PC13 + USART1） |
| `assets/spl-project-template.md` | Keil 工程组织/必配项/占用估算/bring-up/多文件模块化布局 |
| `examples/blink_uart_demo/` | 裸机试例：PC13 点灯 + USART1 打印版本号（已编译运行验证，未上板） |
| `scripts/validate_generated.py` | 静态纪律检查（启发式；VIOLATION 必须清零；含 RTOS/HAL 规则；厂商库目录自动跳过） |
| `tests/bad_sample.c` / `bad_sample_rtos.c` / `bad_sample_hal.c` | SPL/RTOS/HAL 故意违规样本，用于自测检查脚本 |

## 维护与沉淀

随使用逐步完善：发现 API 写法有误、新配方/新坑/检查规则改进时，**征得用户同意后**更新对应文件，
并把进展记入 [ROADMAP.md](ROADMAP.md)；不在单次对话中强行实现未沉淀的能力。
