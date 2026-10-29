# 300W 加热台温控器（F103C6T6 SPL·IAR·HSI 无晶振，实测工程沉淀）

> 来源：D:\stm32project\源代码_241102（IAR EWARM 模版 F103C6T6，开源工程 oshwhub.com/666edaer/220v300w-heating-board，300W 220V 加热台）。
> 状态：文件级读码沉淀，编译产物存在（已编译），上板状态未知。本文是技能首个 **IAR 工程**、首个 **F103 低密度（C6T6/LD）**、首个 **HSI 无晶振设计**、首个 **WFI 低功耗裸机主循环** 的实测样本。

## 1. 架构总览：事件驱动 + WFI 睡眠

```
TIM1 (1.125s 节拍) ──ISR──> MAX6675 测温(去极值平均) → tCurrent → 置任务标志
EXTI15_10 (PB12~15 四键) ──ISR──> 3×1ms 采样判键 + TIM3 setTimeout(100ms) 软件锁 → 置任务标志
主循环: 有 screenTask → screenPageManager()（页面×按键函数指针表）
        有 pidCalTask → pidControlManager()（PID→TIM2 CCR2 低频 PWM→光耦→加热管）
        都没有 → STATE_sleeping + __WFI()
```

- 与前几案对照：四轴=TIM1 时间片轮询、平衡车=EXTI 事件驱动、本工程=**事件驱动+WFI**——按键与测温都由中断置标志，主循环按标志干活，闲则睡眠（`__WFI()`）。裸机低功耗的标准形态。
- 共享纪律良好：全局结构体 `Heater` 成员用 `vu8/vu16/volatile enum`（江协 typedef），ISR 写主循环读的标志位全部 volatile——**是五个实测工程里 volatile 纪律最好的**。
- ISR 里 state 保存/恢复手法：`stateBefore=Heater.state; Heater.state=STATE_measuring; ...; Heater.state=stateBefore;`——调试观测用状态机标记。

## 2. 硬件资源与引脚表（F103C6T6，LD，32KB Flash/10KB RAM）

| 外设 | 引脚 | 配置 | 用途 |
|---|---|---|---|
| HSI PLL | — | HSI/2×16=**64MHz**（无外部晶振） | 系统时钟 |
| TIM1 | — | 36000×2000 @64MHz=**1.125s**，UP 中断 | 测温节拍（注释写 1s，按 72MHz 算的） |
| TIM2 CH2 | PA1 | 27000×2000 @64MHz=**1.1875s** 周期 PWM | 加热功率（光耦，1 格=1%占空比，CCR=duty×20） |
| TIM3 | — | 一次性单例 setTimeout（6400 分频=Xms），lazy init | 按键 100ms 锁/延时回调 |
| SPI1（硬件） | PA5=SCK PA7=MOSI | 64/2=32MHz（注释写 18MHz，72MHz 思维），CPOL0/CPHA1 | ST7735S 屏 |
| LCD 控制 | PA3=DC PA4=CS（**常低**） | BRR/BSRR 直写 | ST7735S 0.96" 160×80 长条屏 |
| MAX6675（位拆） | PA8=SCK PA9=CS PA10=MISO | BRR/BSRR 宏+浮空输入 | K 型热电偶 0~255℃ |
| 按键 ×4 | PB12=+ PB13=set PB14=− PB15=shift | EXTI 双沿（注释写下降沿）+浮空输入 | 设置/菜单 |
| Flash 参数区 | 0x08007800 | 1KB 扇区循环写（第 30 扇区，页尾） | 掉电保存设置 |

- 引脚预算紧凑（LD 封装小）：SPI1 与 LCD/热电偶共享 GPIOA；PA13/14 SWD 保留。
- C6T6（10KB RAM/32KB Flash）跑下整屏 UI+PID+参数库富余——**小器件做完整产品的样板**。

## 3. HSI 无晶振时钟方案 + 时基连锁失准（本工程核心教训）

```c
HSI_SetSysClock(RCC_PLLMul_16);   // RCC_PLLSource_HSI_Div2 → 8/2×16 = 64MHz
```

- 动机：省外部晶振（量产 BOM 成本+板面积）；F1 HSI 8MHz 经 /2×16 PLL 最高 64MHz（**到不了 72MHz**，HSE 才行）。
- FLASH_Latency_2 统一配（≤72M 都安全，注释明确解释）；PCLK1=HCLK/2=32MHz，PCLK2=64MHz。
- **连锁失准清单**（作者全按 72MHz 留下注释与分频，实际 64MHz）：
  | 定时器 | 注释/意图 | 实际 @64MHz |
  |---|---|---|
  | TIM1 测温 | 36000×2000=1s | **1.125s** |
  | TIM2 PWM | 27000×2000=1s 周期 | **1.1875s** |
  | SPI1 | "36MHz/2=18MHz" | **32MHz** |
  - 能跑的原因：加热台热惯性以十秒计，PWM 周期 1.19s vs 1s 无感；SPI 32MHz 屏也受得了。
  - **换时钟源必须全工程重算时基**（坑 55）：grep 所有 Prescaler/Period/注释里的 MHz 数字。
- HSI 方案本身的知识点：`RCC_DeInit→HSI 使能→等待 HSIRDY→Flash Latency→总线分频→PLLConfig(HSI_Div2, ×16)→等 PLLRDY→SW 切 PLLCLK→读 SWS 确认 0x08`——完整可复用序列（含"PLL×16 是 HSI 上限"的约束）。

## 4. 测温链：MAX6675 位拆 SPI + 去极值平均

```c
// 读 16bit：CS 低 → 16×(SCK 高采 MISO→移位→SCK 低) → CS 高
// [14:3]=12bit 温度(0.25℃/LSB)  [2]=热电偶开路标志  开路返回 0xff
temperature = (data >> 3) / 4;    // 丢 0.25℃ 精度换整数，0~255℃
```

- BRR/BSRR 宏直写（`#define SPI_SCK_1 (GPIOA->BSRR = GPIO_Pin_8)`）——单指令置/复位，比 ReadModifyWrite 快且原子；位拆 SPI 无延时宏也可跑（GPIO 翻转本身 ~100ns，6675 上限 4.3MHz）。
- **Smooth 去极值平均的除数 bug（严重）**：
  ```c
  // 读 readTimes=5 次（1+循环4），sum=5 样本和，减 max/min 剩 3 样本之和
  return (sum - max - min) / readTimes;   // ✗ 除以 5，应除 readTimes-2=3
  ```
  读数 = 真实平均 ×(N-2)/N = **×0.6**。后果：显示/闭环温度全低 40%；设定 350℃ 实际 210℃ 就"到温"稳定——**或设定不可达→满功率加热**，靠物理散热平衡兜底。叠加开路风险：热电偶开路返回 0xff=255℃（tSet=350 时 error=95 仍为正→**继续加热**，开路≠停机）。该类设备必须：开路→停机报警；滤波除数=(N-2)。见坑 56。
- Smooth 的正确写法骨架：`for i in N: t=read(); sum+=t; max/min 更新; return (sum-max-min)/(N-2);`

## 5. PID 与加热链

```c
PID = {kp=3.0, ki=0.65, kd=0, maxIntegral=10 /*=(100-kp)/ki 注释给整定依据*/, maxOutput=100}
duty = kp*err + kd*(err-lastErr) + ki*integral;   integral 限幅 ±10，输出 0~100
duty<5 → 0（最小功率死区：光耦/加热管别用极窄脉冲）
CCR2 = duty×2000/100    // 1% = 20 tick；低频 PWM（秒级周期）适合大热惯性负载
```

- 位置式 PID+积分限幅，无积分分离/抗饱和反算（maxIntegral 已够——输出限幅 100 且 maxIntegral=(100-kp)/ki 保证 ki 项不超限，**注释把整定推导写进代码**，好习惯）。
- powerOn 软开关 + heatDutyCycle_max 功率上限（受限插座场景）。
- 执行周期=测温周期 1.125s——加热台量级正确（对照四轴 4ms/平衡车 10ms）。

## 6. Flash 参数区：循环写磨损均衡（小型 EEPROM 模拟）

```c
// 0x08007800 起 1KB（C6T6 第 30 扇区，页尾预留——必须查 .map 确认程序没占）
// 结构体 HeaterStorageStructure{u32×3}=12B，MaxStorageNum=1024/12-1=84 条
storage_findAddress(): 逐条扫 0xFFFFFFFF 找最后一条有效记录
storage_save(): 找空位追加；写满 → ErasePage 整页 → 从头写
storage_read(): 读最后一条，范围校验（duty≤100 且 tSet≤400）失败用默认值
```

- 顺序追加+写满才擦：每页寿命放大 84 倍（10 万次擦写×84≈840 万次保存）——参数保存的标准低成本方案。
- **掉电安全缺口（坑 57）**：写满时**先擦后写**，掉电窗口内全丢；且写一条 12B 中途掉电会留半条记录（读时范围校验能挡住值域内的半条记录吗——结构体 3×u32 逐字写，写到第 2 字掉电：前 2 字有效值+第 3 字 0xFFFFFFFF，`initDelay=0xFFFFFFFF` 非法但读取校验只查 duty/tSet 两项 → **initDelay 读到垃圾**）。升级模式：双扇区交替+序号字（新页写完标 valid 再擦旧页）。
- `storage_look()` 调试残留（`if(1)` 开关+读页外 p[-i]）——发布未清。

## 7. 屏幕与交互

- **ST7735S 0.96" 160×80 长条屏**（首见尺寸）：GRAM 132×162 中开窗，`LCD_LINE_OFFSET=24` 行偏移（`LCD_SetAddress` y+24）——**长条屏偏移是这类 0.96/1.14 寸屏的标配坑**，别屏不亮先查 offset。
- 初始化极简：0x11 SleepOut（前后 120ms）→0x3A=0x05 RGB565→0x36=0x78（MY|MX|BGR 横屏）→0x29。代码附 0x36 方向位速查注释表（0x00/0xC0/0x70/0xA0 四角原点）。
- **IAR 特有**：`#pragma optimize = low` 标在 SPI_SendData8/SendCmd 上，注释"避免IAR编译优化出错"——IAR 高优化档破坏 SPI 位时序的经验规避；移植到 Keil/GCC 无此 pragma，删。StartUp/core（core_cm3.c IAR 版）与 Keil 版不通用（main.c 头注释自述）。
- **页面×按键 双维函数指针表**（首见范式）：
  ```c
  void (*pageLoad[5][5])() = { {page_setT,...}, /* [当前页面][按键事件]→绘制函数 */ };
  ```
  UI 跳转零 if-else；第 5 行是 welcome 的冗余复制行（表冗余无害但占行）。双级光标菜单（optionIndex 选项/setIndex 值，shift 键切换）。
- 温度→颜色渐变：`color = 0xf800 + (50-T×50/400)<<5`（低温偏黄高温纯红，RGB565 内联算）。
- `LCD_ResetCS()` 每次刷屏前复位 CS"避免上一次卡死"——异常自愈手法。

## 8. Bug 与现场教训清单

1. **Smooth 除数错（×0.6 读数）+ 开路 0xff 无故障处理** → 详见 §4，安全级。
2. **时基连锁失准**：§3 表，HSI 改造后 4 处 72MHz 遗留。
3. EXTI 注释"下降沿触发"实际 `EXTI_Trigger_Rising_Falling` 双沿——双沿+100ms 软件锁的组合里，双沿是必要的（按下抬起都能锁），注释错。
4. EXTI ISR 内 `delay_ms(1)×3` 采样去抖——阻塞 3ms（validator VIOLATION，属实）；更好：EXTI 只记时间戳，1ms 节拍里采样（本工程无 1ms 节拍，TIM3 可兼任）。
5. `if(1){...} else {仿真数据}`——仿真/实测分支用 if(1) 硬开关留在交付代码（TIM1 ISR 内 20 行仿真模型）。
6. pageLoad 表第 5 行冗余；`u8 optionStr[][17]` 栈上二维数组每帧重建。
7. storage_look 调试残留 + 读页外负地址。
8. TIM1 ISR 内测温（位拆×5 次 ≈ 数百 µs，6675 转换期 SCK 无碍）——轻量可接受，但加了 `delay` 类宏就变坑；边界记录。

## 9. validator 记录

- 8 用户文件：1 VIOLATION 3 WARNING，**全真实零误报**：
  - tasks.c:111 EXTI ISR 内 delay_ms（去抖采样）——真违规（§8.4）
  - tasks.c:142/156 HSI/PLL 就绪等待无超时——时钟启动等待，实际不会卡（HSI 起振失败=芯片已坏），可接受豁免类
  - lcd:44 SPI TXE 忙等无超时——LCD 拔线即冻结，UI 设备可接受（有 LCD_ResetCS 自愈尝试）
- LD 密度向量与 MD 表兼容（默认 --density md 无误报）。

## 10. 生成检查单（同类"温控/低速执行器"工程）

1. 选 HSI 无晶振方案先答三问：需要 72MHz 吗？USB 吗（要 HSE）？容差（HSI ±1%）够吗（异步串口高波特率会偏）？——然后全工程时基用 64MHz 重算，注释同步。
2. 温度/传感类均值滤波：去极值后除数=N−2；写完用恒温源（冰水/体温）校一点。
3. 热电偶/传感器开路值必须映射到**停机+报警**，绝不能是"看起来很高/正常的数"。
4. 加热/功率链三保险：软件开关、最大占空比限幅、最小占空比死区（<5%→0）。
5. Flash 参数区：页尾预留+查 .map；追加式磨损均衡；掉电安全用双区交替+valid 标志；读后必做范围校验+默认值兜底（校验要覆盖**每一个**字段，不只前两个）。
6. EXTI 多键共用向量：逐 pin 查标志，双沿+软件锁记在注释里；去抖采样放节拍里不放 ISR。
7. WFI 主循环：所有唤醒源（ISR 置标志）对应变量 volatile；睡前再查一次标志防丢唤醒。
8. IAR→Keil 移植：core/startup 换套、`#pragma optimize` 删、工程文件重配；代码层 SPL 全兼容。
9. 长条 LCD（160×80 等）：坐标 offset（LCD_LINE_OFFSET）写进驱动宏，初始化序列（0x11/0x3A/0x36/0x29）最简四条即可点亮。
10. 表驱动 UI：页面×事件函数指针表适合 ≤5×5 规模；表外的按键长按/连击逻辑另配状态机。
