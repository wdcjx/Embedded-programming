# 两轮平衡小车（SPL 实测工程沉淀）

> 来源：用户实测工程 `D:\stm32project\STM32平衡小车测试2`（STM32F103C8，SPL，Keil）。
> 状态：**已上板跑通**（编译通过、直立可用）。validator 对用户代码报 9 VIOLATION，全部为真实纪律问题（见 §8）——
> 工程本身能跑，本文沉淀其结构、算法与现场妥协，同时记录"能跑但违纪"的典型样本。
> 关联文档：[quad-drone-spl.md](quad-drone-spl.md)（四轴串级 PID/时间片调度——对照读）。

## 1. 架构：事件驱动控制（与四轴时间片对照）

**没有定时器中断节拍**。10ms 控制周期由 MPU6050 DMP 的 INT 引脚（PB5，下降沿）触发 EXTI9_5_IRQHandler 驱动——
DMP FIFO 100Hz 输出与控制周期天然同步，控制时机即数据就绪时机（无轮询空转、无数据撕裂窗口）。

| 维度 | 平衡小车（本工程） | 四轴（quad-drone-spl） |
|---|---|---|
| 节拍源 | MPU6050 INT → EXTI（100Hz 事件驱动） | TIM1 1ms 定时中断时间片（4ms 控制片） |
| 控制环位置 | EXTI ISR 内联三环计算 | TIM1 ISR 内联 Fly_task |
| 传感器读取 | ISR 内软 I2C 读 DMP FIFO（阻塞数百 µs） | 主循环 5ms 读，ISR 只算 PID |
| 主循环职责 | OLED 显示 + 蓝牙解析 + 电池 ADC | NRF 收包 + DMP 姿态 + 显示 |

两种都属"控制进 ISR"范式（坑 44）：OLED 刷屏几十 ms 阻塞主循环，控制环被迫进中断。

## 2. 三环控制算法（平衡小车之家结构）

```
速度环(PI) ──► Velocity_out（≈目标角度修正量，deg 域）
                      │ ×Vertical_Kp（折算成 PWM 域）
直立环(PD) ──► Vertical_out ──(减)──► PWM_out ──► ±Turn_out ──► MOTO1/MOTO2
转向环     ──► Turn_out ──────────────┘              (±5000 限幅)
```

- **直立环 PD**（`Control.c:127`）：`PWM_out = Vertical_Kp*Angle + Vertical_Kd*gyro_Y`
  本工程 Kp 直接乘当前角（Roll），**机械中值 Med_Angle 虽然传参但函数体未使用**（见 §7 bug1）；
  标准式应为 `Kp*(Angle-Med)+Kd*gyro`——零点全靠机械装配+速度环补偿。
- **速度环 PI**（`Control.c:140`）：偏差低通滤波（a=0.7：`low=(1-a)*err+a*last`）→ 积分（限幅 ±10000）
  → `Velocity_Kp*low + Velocity_Ki*integral`；摔倒（stop=1）自动清积分（再站起无积分突跳）。
- **转向环**（`Control.c:168`）：`Turn_Kd*gyro_z + Turn_Kp*RC`——Kd 项是转向约束（无指令时抑制自转），
  Kp 项是遥控响应。**本工程 Turn_Kd 恒 0（两分支同赋 0，见 §7 bug2）**，只剩遥控项。
- **输出合成**：`PWM_out = Vertical_out - Vertical_Kp*Velocity_out`——速度环输出本质是角度修正量，
  乘直立环 Kp 折算到 PWM 域再从直立输出中减去（速度环不直接进 PWM，而是"改目标角"的近似）。
  这与四轴的**串级**（外环输出=内环设定值）不同——平衡小车是**并联合成**结构。

### 实测参数（10ms 周期组，`Control.c:26-43`）

| 环 | Kp | Kd/Ki |
|---|---|---|
| 直立 | 540 | Kd=1.8 |
| 速度 | 0.3 | Ki=0.0015 |
| 转向 | 40（Kp） | Kd=0（失效） |

注释中另有一组 20ms 周期参数（550/2.5/0.8/0.004），未启用——**切换周期必须重调参数**，10ms↔20ms 参数不通用。

### 蓝牙遥控（积分斜坡）

指令字节经 USART2 RXNE 中断锁存到 `Res`，主循环 `BT04_Control()` 映射 `car`：A/B=前/后，C/D=左/右，E=停。
控制 ISR 内每周期 `Target_Speed++/--`（限幅 ±18）、`Turn_Speed±10`（限幅 ±70）——**积分式斜坡**使速度指令平滑，
松手（E）清零。缺陷：`Res` 永不超时清零，蓝牙断联=保持最后指令（见 §6 安全链）。

## 3. 硬件资源全表

| 资源 | 引脚/配置 | 用途 |
|---|---|---|
| USART1 | PA9/PA10，115200 | printf 调试（正点原子 fputc，TC 忙等） |
| USART2 | PA2/PA3，9600 | BT04 蓝牙遥控（RXNE 单字节锁存） |
| TIM1 CH1/CH4 | PA8（左）/PA11（右），10kHz（ARR=7200） | 电机 PWM；**MOE 已开**（高级定时器必须） |
| GPIO 方向 | PB14/15（左）PB12/13（右） | H 桥方向（符号→方向脚+正 CCR） |
| TIM3 编码器 | PA6/PA7，TI12 四倍频 filter=10 | 左轮速度（10ms 读 CNT 清零） |
| TIM4 编码器 | PB6/PB7 | 右轮——**接触不良，实际未用**（§5） |
| MPU6050 软 I2C | PB10 SCL/PB11 SDA（正点原子位带版） | DMP 姿态 100Hz + 陀螺仪原始值 |
| MPU6050 INT | PB5，EXTI9_5 下降沿，抢占 0 | 控制节拍源 |
| SR04 超声波 | PA0 Trig/PA1 Echo，TIM2 1µs 计数 | 测距（量程截断 ~25cm，§5） |
| ADC1 CH4 | PA4 | 电池电压（÷5 分压） |
| OLED | PB8/PB9 软 I2C | 江协 SSD1306 版（ShowSignedNum 等） |

定时器分光：TIM1=PWM、TIM2=SR04 计时、TIM3/4=编码器、SysTick=Delay——C8T6 资源刚好铺满。

## 4. 关键实现细节

- **符号化 PWM 输出**（`Motor.c Load()`）：moto≥0 → 方向脚正转+`TIM_SetCompare(正 CCR)`；moto<0 → 反向脚+`-moto`。
  CCR 永远收正数，符号交给方向脚——H 桥驱动的通用写法。
- **编码器读取**（`Read_Speed`）：`(short)CNT` 后立即清零——16 位自然回绕经 short 转换即得带符号速度，无需中断计数。
- **SR04 时序**（`SR04.c`）：Trig 13µs 脉冲→TIM2 CNT=0→忙等 Echo 上升沿（超时 100µs）→开 TIM2 计数→忙等回波
  （**超时 1500µs→量程 ~25cm 截断**）→count/58=cm。全程阻塞 ~1.7ms，**且被放在 10ms 控制 ISR 里调用**（§7 bug6）。
- **DMP 初始化**：`MPU_Init()`（±2000dps/±2g/100Hz/LPF=50Hz/INT 低有效）→`mpu_dmp_init()`（FIFO 100Hz）。
  正点原子 inv_mpu 全家桶，与本库 FC 工程同源。
- **电池电压**：`ADValue/4096*3.3*5`——单次软件触发阻塞转换（主循环内，µs 级可接受）。

## 5. 现场妥协（实测工程的真实一面）

1. **右编码器接触不良** → `Encoder_Right = Encoder_Left`（`Control.c:69`，注释自认"上面才是对的"）：
   单编码器数据喂双轮速度环——左右轮差异全部丢失，速度环退化为"平均速度控制"。桌面调试可接受，
   正式修复=换编码器线材/插座。
2. **SR04 量程 25cm**：回波超时 1500µs 截断（1500/58≈25.8cm）——桌面避障够用，量程外一律返回 25。
3. **TIME.c 整文件注释**：TIM2 1ms 节拍的旧方案遗迹（后改为 EXTI 事件驱动）——架构演进痕迹，死文件应删。

## 6. 安全链对比（平衡小车 vs 四轴）

| 保护 | 平衡小车（本工程） | 四轴（quad-drone-spl） |
|---|---|---|
| 摔倒停机 | ✅ \|Roll\|>40 → 双轮 0 + 清速度环积分 | —（飞行器无此概念） |
| 上电默认 | ❌ 无锁定机制 | ✅ Lock=1 + connect_protect=1 双保险 |
| 失联保护 | ❌ **蓝牙锁存无超时——断联=保持最后指令一直跑** | ✅ 心跳双窗 1s → 关电机 |
| 低电保护 | ❌ 仅显示电压 | ✅ ≤3.5V Battery_Protect 停转 |

给遥控移动设备的建议：收端指令必须带**超时衰减**（如 500ms 无新字节 → Target_Speed 向 0 衰减），
或心跳+失联全停——平衡小车能摔，四轴能飞丢，场景不同但链路纪律同源。

## 7. Bug 清单（开源/学习工程典型样本）

1. **Med_Angle 死参数**（`Control.c:127`）：`Vertical(Med,Angle,gyro)` 传入 Med 但函数体只用 Angle——
   "在这里修改你的机械中值"的调参旋钮实际不起作用；标准式 `Kp*(Angle-Med)+Kd*gyro` 才对。
2. **Turn_Kd 双分支同值**（`Control.c:90-91`）：注释宣称"开/关转向约束"，两分支都赋 0——
   转向环 gyro 约束项恒失效，环退化为纯遥控比例。
3. **TIM4_IRQHandler 检查 TIM3**（`Encoder.c:96`）：复制粘贴未改——TIM4 溢出标志永远不清。
4. **编码器 NVIC 从未使能**：`TIM_ITConfig(Update)` 开了外设中断源，但 TIM3/TIM4 的 `NVIC_Init` 缺失——
   ISR 根本不会执行。与 bug3 **互相抵消**：标志不清也没人进 ISR（10ms 清 CNT 使回绕极罕见，侥幸无害）。
   这是"哑中断"典型：开了 IT 源≠开了 NVIC 通道。
5. **NVIC 分组中途切换**：全工程唯一 `NVIC_PriorityGroupConfig(Group_3)` 在 `BT04_Init()`，
   而 main 里 `uart_init()` 先做了 USART1 `NVIC_Init(3,3)`——先编码后换组，字面优先级被硬件按新分组重新解释。
   （详见坑 49。）
6. **SR04 阻塞调用进控制 ISR**：~1.7ms 忙等挂在 10ms EXTI 控制节拍内（预算内能跑，纪律上违纪）。
7. **printf 回显在 USART1 ISR**（正点原子模板）：`printf("%c",Res)` 在 RXNE 中断内，TC 忙等 87µs/字节。
8. **蓝牙指令锁存无超时**：断联保持最后指令（§6）。

## 8. validator 记录（9 VIOLATION，全真实零误报）

| 文件 | 违规 |
|---|---|
| BT04.c | ISR 访问非 volatile `Res`（×1） |
| Control.c | EXTI ISR 访问非 volatile `Distance/Med_Angle/Target_Speed/Turn_Speed/stop`（×5） |
| Usart.c | ISR 访问非 volatile `USART_RX_BUF/USART_RX_STA`（×2）、ISR 内 printf（×1） |

未抓到但人工发现：bug3/4（ISR 查错定时器+哑中断）、bug5（NVIC 分组顺序）、bug1/2（死参数）——
静态纪律检查的边界即在此，工程消化时必须配合人工读码。

## 9. 生成检查单（平衡车类需求）

1. 控制节拍选型：传感器 INT 事件驱动（DMP 100Hz）或定时器时间片——二选一并说明取舍；
2. 三环输出结构画框图交付（直立 PD / 速度 PI / 转向），标明每个 Kp/Ki/Kd 的作用域与单位域；
3. 机械中值、转向约束等调参旋钮必须真实生效（grep 确认参数被使用）；
4. 摔倒保护（角度阈值停机+清积分）与失联保护（指令超时衰减）缺一不可；
5. 编码器：TIM_ITConfig 与 NVIC_Init 成对决策——溢出中断要不要开，两处必须一致；
6. NVIC 分组在 main 第一行设置且全工程唯一；电机 PWM 用高级定时器时 MOE 必须使能。
