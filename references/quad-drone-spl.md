# 2.4G 四轴遥控/飞控双机系统（F103C8T6 SPL 实测工程）

> 来源：D:\stm32project\遥控代码 与 D:\stm32project\飞控代码（用户实测工程，已上板飞行）。
> 两片 F103C8T6 + 标准库 + Keil，无 RTOS。遥控=江协模板家族布局（Start/Library/System/User），
> 飞控混有正点原子血统（sys.c/My_GPIO_Init/PBout 位带/inv_mpu 移植底座）。
> 状态：读码级实测（未逐行上板复测），validator 23 文件仅 1 处误报（已修，见 §11）。

## 1. 总体架构

```
遥控端（手持）                          飞控端（机架）
─────────────                          ─────────────
摇杆×4 ADC+DMA ─┐                       NRF24L01 收帧(7ms) ──> Data_Proc(21ms)
矩阵键盘 8 键    ├ Data_Proc(11ms)       MPU6050+DMP(5ms) ──┐
编码器(航向微调) ┘   │                                        ├ Fly_task(4ms,ISR!)
OLED 5页菜单    Data_Display(16ms)      摇杆指令+姿态 ──────┘   串级PID×3轴
                Data_Send(5ms) → NRF24L01  ───9字节帧───>       ↓
                心跳 500ms++            X型混控 → TIM2 4路PWM(20kHz) → 电机
                                        安全门: Lock & connect_protect & 低压
```

- 双端同为 **TIM1 1ms 更新中断做系统节拍**，ISR 里只做计数器分频（+1、与阈值比较清零），任务体在主循环里按标志轮转（`if(Send_Slow_time==0){任务; Send_Slow_time=1;}`）。
- **唯一例外：飞控 `Fly_task()`（PID+混控+Motor_Control）直接在 TIM1 ISR 里每 4ms 跑**——因为 OLED 软 I2C 刷屏一次要几十 ms，主循环会被卡住，强实时控制环只能放中断。代价见坑 44（F103 无 FPU，ISR 里跑 6 组软件浮点 PID）。
- 无 USART：遥控无串口打印，调试全靠 OLED 页面与 LED。

## 2. 引脚表（实测读码）

**遥控端**
| 外设 | 引脚 | 说明 |
|---|---|---|
| 摇杆 ADC1+DMA1_CH1 | PA0/PA1/PA3/PA4 | 5 通道循环扫描，秩=[CH4,CH3,CH0,CH1,CH2]，`Rocker_Value[0..4]` 对应 PA4/PA3/PA0/PA1/PA2 |
| 电池 ADC | PA2(CH2) | 复用摇杆扫描第 5 通道，×2 分压 1S 锂电 |
| 矩阵键盘 | 选通 PB12/PB6；读取 PB0/PA6/PA7/PB11 | 2×4=8 键，输出逐组拉低扫描 |
| LED×4 | LOCK=PB10、UNLOCK=PB1、LDRIFT=PB7、RDRIFT=PA5 | PCB 上锁/解锁 LED 反接，软件补救（swap 逻辑） |
| 编码器 | TIM3 CH1/CH2=PB4/PB5 | `GPIO_PartialRemap_TIM3` + `SWJ_JTAGDisable`；TI12 四倍频、滤波 0xF、读后清零 |
| OLED(SSD1306) | PB8=SCL PB9=SDA | 江协软 I2C 正本 |
| NRF24L01 软 SPI | SCK=PA8、CSN=PA11、CE=PA12、MOSI=PB15、MISO=PB14、IRQ=PB13 | GPIO 位拆 MSB-first |
| 系统节拍 | TIM1 1ms | 抢占 0 子 0，Group_2 |

**飞控端**
| 外设 | 引脚 | 说明 |
|---|---|---|
| 电机 PWM | TIM2 CH1~4=PA0~PA3 | 20kHz 无声（ARR=3600-1,PSC=0），PWM1 模式，初始 Pulse=0 |
| NRF24L01 软 SPI | CSN=PA4、SCK=PA5、MOSI=PB6、MISO=PB7、CE=PB14、IRQ=PB15 | 与遥控同一套驱动，引脚不同 |
| MPU6050 软 I2C | PB10=SCL PB11=SDA | 位带 `PBout(10/11)` 宏、推挽写+切浮空读、4µs≈125kHz、ACK 250 次超时 |
| OLED(SSD1306) | PB8/PB9 | 驱动内嵌自己的软 I2C（与 MPU 的独立并存） |
| 电池 ADC | PB1(CH9) | 单次阻塞转换，×2 分压 1S，100 次滑动平均 |
| 系统节拍 | TIM1 1ms | 抢占 1 子 1，Group_2（注释里留着旧双定时器方案 TIM3 独立控制环的迭代痕迹） |

注意遥控端 CSN/CE 用了 PA11/PA12——这是 USB D-/D+ 引脚，不用 USB 才可挪用；PB13/14/15 在 JTAG 释放后也可用（本工程 JTAG 已禁）。

## 3. 时间片调度（TIM1 1ms 分频）

ISR 模式统一为 `if(++Xxx_Slow_time > N) Xxx_Slow_time = 0;`，主循环见到 0 就执行并置 1。
**周期 = N+1 毫秒（不是 N）**——该工程注释全部写 N（"每3ms处理飞控算法"实际 4ms），读码时以代码为准（坑 46）。

| 遥控任务 | 周期 | 飞控任务 | 周期 |
|---|---|---|---|
| Data_Send（组帧+无线发送） | 5ms | Fly_task（PID+混控，**ISR 内**） | 4ms |
| Data_Proc（摇杆映射+调参编码） | 11ms | Attitude_Data_Proc（DMP 读姿态+陀螺） | 5ms |
| Data_Display（OLED 刷屏） | 16ms | Data_Receive（NRF 收帧解析） | 7ms |
| LED_Control | 31ms | GetCur_Power（电池均值） | 11ms |
| 心跳：connect_time>500 → connect_value++（200 回绕） | 500ms | Data_Proc（指令映射+PID 刷新） | 21ms |
| | | Data_Display（OLED） | 51ms |
| | | LED_Detect | 每 1ms |
| | | Connect_Portect_Test（失联判定） | 1000ms |

## 4. 无线链路（NRF24L01）

- **软件 SPI**（MySPI_SwapData 位拆），Enhanced ShockBurst 自动应答：EN_AA=P0、SETUP_RETR=0x1A（ARD=250µs、ARC=10 次——代码注释"580us"失实）、RF_CH=0、收发地址同为 `0xF0F0F0F0F0`（5 字节）。
- **双向半双工**：两端平时 CONFIG=0x0F 常驻接收；发送时切 0x0E→CE 脉冲→`while(NRF24L01_R_IRQ()==1)` **死等 IRQ（无超时）**，靠 ARC=10 次重发耗尽自然退出，最坏阻塞 ≈4ms，贴着 5ms 发送节拍（坑 45）。收帧后 FLUSH_RX + 写 1 清 STATUS + `Delay_us(150)` 稳定。
- **9 字节帧协议**（Send_Data[0..8]）：

| 字节 | 遥控→飞控含义 |
|---|---|
| [0] | Lock 锁存（1=上锁） |
| [1] | 油门 Oil_Get（左摇杆 0~100） |
| [2] | 俯仰指令 getpitch（右摇杆 0~100） |
| [3] | 翻滚指令 getroll（右摇杆 0~100） |
| [4] | 航向 Yaw_Control（编码器微调映射 10~190） |
| [5] | connect_value 心跳（500ms 自增，200 回绕） |
| [6] | index_type 0~21（0=不变；1~3 机械零点；4~21=18 个 PID 参数选择） |
| [7..8] | PID_Data_Send 两字节浮点编码 |

- **浮点两字节编码**：发送端 `[0]=(uint8_t)(v*10)`、`[1]=(uint16_t)(v*1000)%100`；接收端 `v = [0]*0.1 + [1]*0.001`（机械零点类用 +100 偏移域）。精度 0.001、量程 ≤25.5——够 PID 用，省了浮点序列化。
- **失联判定**（飞控）：`Connect_Portect_Test()` 每 1s 被 TIM1 ISR 调用；connect_bit 交替"存值/比较"，两个 1s 窗口收到的 connect_value 都不变 → connect_protect=1。恢复靠收到新帧时 connect_value 变化。**上电默认 connect_protect=1**——没连上就永不转电机。

## 5. 串级 PID（Data_Proc.c，飞控核心）

**结构**：角度外环（输入 Angle+Gyro，输出目标角速度 ±300°/s）→ 角速度内环（输入外环输出+Gyro，输出 PWM 修正量 ±1000）。俯仰/翻滚/航向三轴同构复制三份。

**工程细节（值得抄的部分）**：
- **积分分离**：|err| < 3.0 才累加积分（大偏差只走 P/D，防起飞瞬间积分爆炸）。
- **积分限幅**：fminf/fmaxf 双向（外环 ±500、内环 ±1200）。
- **反向计算抗饱和**：输出超限时 `Integral -= (Out - 限幅值)/Ki`——把超限部分回灌掉。
- **油门门限清积分**：`Oil_Set <= PID_Oil_Min(360)` 时 Integral 直接=0——油门拉低/落地即自动复位积分，起飞无积分突跳。
- **Ki 除零保护**：`fabsf(Ki) > 1e-6f` 才做积分与反向计算。
- **外环 D 项用 Gyro 阻尼**（不是角度差分）：`D = Kd*Gyro`，角度噪声不被微分量放大——平衡小车经典写法；内环 D 项才是误差差分 `Kd*(err-err_last)`。
- 摇杆死区：40~60 映射 0°，两侧线性到 ±10°；航向 `getyaw_SET = getyaw - 100`（编码器 10~190 → ±90°）。
- PID 参数**权威源在遥控**（飞控 flash 默认值外环全 0），飞控每 21ms 用收到的帧全量刷新 18 个系数——遥控端按键调参即时生效。

**实测系数**（遥控 RC_Variable.c 出厂值）：外环 Kp=2.85 Ki=0.003 Kd=0.048（三轴同）；内环 Kp=4.0(俯仰)/4.0(翻滚)/4.0(航向)、Ki=0、Kd=0.025。飞控 flash 默认内环 5.2/1.00/1.5——被遥控值覆盖，仅首帧前短暂有效。

## 6. 混控与电机

```
PWM_OUT1 = Oil + Pitch - Roll + Yaw     (左前)
PWM_OUT2 = Oil + Pitch + Roll - Yaw     (右前)
PWM_OUT3 = Oil - Pitch - Roll - Yaw     (左后)
PWM_OUT4 = Oil - Pitch + Roll + Yaw     (右后)
```
- X 型四轴；Oil = Oil_Get×36（0~100 → 0~3600=ARR）。Limit_PWM 钳 360~2880（10%~80%）。
- **Motor_Control 是安全汇聚点**：`if(Lock==0 && connect_protect==0) 输出 4 路 CCR，else 全 0 停转`。上锁、失联、低压（Battery_Protect 置 1 后油门路径仍走混控但 protect 门独立）任一条件命中即关电机——**失联保护是"停转"而非"清油门"**（main 注释写"自动赋予油门值为0"与实现不符，坑 46）。
- 安全链全貌：遥控上电 `Lock=1`（默认上锁）→ 飞控上电 `connect_protect=1`（默认失联态）→ 双保险，任意单端复位都不会转电机。

## 7. 遥控调参 UI

- OLED 5 页：0 主菜单 / 1 摇杆数据 / 2 机械零点（俯仰/翻滚/航向重置）/ 3 PID 参数（6 组×3 系数=18 项+精度）/ 4 电量。左侧固定菜单栏+反色高亮。
- 矩阵键盘 8 键：1 上 / 2 下 / 3 数值减 / 4 数值加 / 5 返回 / 6 确认 / 7 上锁 / 8 解锁。`Key_Tick()` 每 10 次采样一次（软件去抖）、**松手沿触发**（CurrState==0 && PrevState!=0）。
- 调参精度三档：Tuning_precision={0.001, 0.01, 0.1} 循环切换。减方向钳 ≥0（加方向不钳——单向越界靠人眼，偷懒但实测没炸）。
- 航向微调：编码器计数值 `/2+100` 映射 10~190，左右漂移 LED（LDRIFT/RDRIFT）指示当前偏航方向。
- 航向零点重置（index_type=3）是个**跨端触发器**：遥控把 fly_yaw_zero 递增发出，飞控发现值变化 → 现场 DMP 读一次 Yaw 存为新零点——避免存储绝对角（DMP yaw 会漂移，存差值更稳）。

## 8. 姿态与传感器

- MPU6050 + inv_mpu DMP 硬件解算（正点原子移植版）：初始化序列=复位→唤醒→±2000dps/±2g/200Hz→关 INT/FIFO→CLKSEL=PLL→读 ID 校验。姿态每 5ms `MPU6050_DMP_Get_Data()`，同时读原始陀螺 `/16.4` 转 °/s 给内环。
- `gyro_filter()`（0.98 一阶互补）**定义了但无人调用**——死码（坑 46 旁证：开源工程死码要识别不要照抄）。
- 电池：飞控 PB1 单次阻塞转换×2 分压，100 次滑动平均 ≤3.50V 置 Battery_Protect；遥控复用摇杆 DMA 第 5 通道。遥控 PowerDetection.c 是死模块（main 未调用，系数还是旧 2S 遗留 ×26.4 与活代码 ×6.6 并存）。
- 飞控 MyI2C.c（PB10/11 完整软 I2C）与 MPU6050_I2C.c 同引脚并存，无人调用——版本考古痕迹（OLED 用内嵌、MPU 用独立实现），**死码占了编译体积还埋引脚冲突隐患**。

## 9. 上电时序与编译产物

- 遥控 main：OLED→NRF→Key→Rocker_ADC→Timer→LED→Encoder；飞控 main：Timer→OLED→NRF→Motor→MPU6050_Init→DMP_Init→GetBattery→LED。共同点：**TIM1 最后才开**（遥控）或最先开（飞控）——不统一但都先完成外设初始化再进调度。
- 两工程 GBK/UTF-8 混编（约半数文件 GBK），入库或跨编辑器协作前应统一转 UTF-8（坑 36 同款）。
- Keil 树：遥控 Hardware/ 下 13 模块 + System(Delay/Variable)；飞控多 Mpu6050/ 子目录（inv_mpu.c 87K + dmp 驱动 57K）。MyI2C.c 遥控端 0B 空文件也在树里。

## 10. 生成同类系统时的检查单

1. 控制环周期（4ms）与姿态采样（5ms）不成倍数也无妨（PID 是异步采样的），但 **Send 死等 + 5ms 节拍 + ARC 重发**三者叠加的余量必须算清（坑 45）。
2. ISR 内跑 PID：所有被 ISR 读、主循环写的变量按 ISR 纪律处理（volatile + 多变量组快照，坑 44）；或学 CoreScribe 用 pending 标志延迟到任务层。
3. 失联保护三要素：心跳递增（发送端）→ 双窗比较（接收端）→ 执行点收敛到一个函数（Motor_Control），上电默认保护态。
4. 调参系统：参数权威端唯一（遥控），接收端每周期全量刷新（幂等，丢帧自愈）；浮点走两字节定点编码即可。
5. 混控公式与电机转向、桨叶正反一一对应，写死成宏并注释电机物理位置。
6. 上锁/解锁是无线帧里的普通字节——**上锁状态要在遥控断电重启后默认上锁**（本工程 Lock=1 出厂值 ✓）。

## 11. validator 与本工程

- 23 个用户文件仅 1 处 VIOLATION：`NRF24L01_R_IRQ()`（读 IRQ 引脚的普通函数）被旧规则「函数名含 IRQ 即按向量名校验」误伤。已修 validate_generated.py：仅 `_IRQHandler` 后缀进向量校验；含 IRQ 的其他名字须为已知向量的真前缀（截断拼写，如 WWDG_IRQHandle）才算疑似向量。回归：bad_sample 10 违规不变、模板 PASS 不变、截断拼写用例仍被捕获。
- 其余文件 0 VIOLATION；ISR 纪律类（Fly_task 在 ISR 跑 float PID、全工程无 volatile）属架构决策+历史遗留，validator 单文件启发式不覆盖跨文件共享——正是坑 44 要人工审的原因。
