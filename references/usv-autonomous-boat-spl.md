# USV 无人水面艇自主导航（SPL 裸机 + 树莓派双层架构）

> 源工程：USV-STM32F103-part-master（Estello_UAS，Keil MDK + STM32 标准外设库）。
> 目标芯片 **STM32F103ZE**（高密度，LQFP144）。实测上水航行（作者录有演示）。
> 技能库定位：首个水面机器人样本、首个 **u-blox UBX 二进制 GPS** 样本、首个 **Madgwick 9 轴姿态**样本、首个 **遥控 PWM 捕获**样本。
> 本文档由逐文件读码提炼（2026-11，v0.10.6），validator 全量记录见 §10。

## 1. 工程档案与总体架构

```
        ┌──────────── 遥控器（270MHz 接收机 PWM 输出）────────────┐
        │ CH0 模式三档 │ CH1 电机使能 │ CH2 转向 │ CH3 油门          │
        └──────────────────┬───────────────────────────────────┘
                           │ TIM3 四通道 PWM 捕获（20µs 精度）
┌──────────────────────────▼──────────────────────────────────────┐
│ STM32F103ZE 下位机                                               │
│  TIM5 0.05ms 基础脉冲 → 嵌套取模分频树 200/100/50/10/5/1 Hz      │
│  50Hz  MadgwickAHRSupdate(9 轴) + 四元数→欧拉（增益自动衰减）     │
│  10Hz  状态机切换 / 自主导航 Execute_Planned_Path / 遥控解析      │
│  5Hz   树莓派指令收帧 / 回传遥测帧                                │
│  1Hz   喂狗 / 树莓派离线计数                                      │
│  USART3 115200 ← u-blox M8N（UBX NAV-PVT 5Hz, DMA+IDLE）         │
│  USART1 921600 ↔ 树莓派（Eastar 0.2 协议）/ LoRa 无线串口可切     │
│  软 I2C PC11/PC12 ← HMC5883L 罗盘 + MPU6050（陀螺+加计）         │
│  TIM2/TIM4 编码器（TI12 四倍频+中断计圈）× 2 推进轮              │
│  TIM8 50Hz 低有效 PWM：CH1/2 电机 + CH3 舵机（MOE 已开）          │
│  IWDG ≈3s                                                          │
└──────────────────────────┬──────────────────────────────────────┘
                           │ 22B 帧（lon/lat/yaw/pitch/roll/hAcc/状态）
                    树莓派上位机（规划航点 yaw+distance+EndOfNav）
```

- **双层分工**：STM32 管实时控制与姿态，树莓派管规划；链路断开 2 秒（`MotorLockTimeAfterRaspiOffline`）自动锁电机——水面版的失联保护（对照四轴 NRF 心跳双窗、平衡车无保护）。
- 上电默认 `STATE_MANUAL_CTRL`（手动），遥控 CH0 三档（1200/1800 阈值）切自动/采点/手动/调试，安全起步 ✓。
- 状态 5 态：INITIALIZE / NORMALLY_RUNNING（自主）/ COLLECTING_POINTS（GPS 采点）/ MANUAL_CTRL / DEBUGGING；`CHANGE_STATE_TO(Analyze_Controller_Msg())` 10Hz 驱动，case 刻意贯穿共享 LED+增益衰减段。

## 2. CONFIGURATION.h 配置驱动架构（反常规组织样本）

工程不用常规 xxx.h，而是**根目录单文件 CONFIGURATION.h 按 `#ifdef 模块名` 分节**：每个模块 .c 首行 `#define GPS 1` 再 include 它，对应节里展开该模块的 include/宏/全局变量定义。

- 优点：全部模块参数一处可查（磁偏角、BETA、PID 参数、编码器线数、缓冲长度）；改一处即全模块生效。
- 代价：**任何模块重编译都牵动全头**；变量定义在 .h 里靠 `#ifdef 模块名` 保证单实例（漏 define 即链接重复定义/未定义）；IDE 跳转索引混乱。
- 关键配置：`USE_BRUSHED_MOTOR=1`、`USE_USART3_GPS_DMA_RX=1`、`USE_USART1_COMM_DMA_TX=1`、`USE_USART1_COMM_DMA_RX=0 //DMA_Rx目前不可用`（作者自认的退化）、磁偏角 8°、`MOTOR_MAX_SPEED=6`（0.1s 内最大位移 cm）、`ENCODER_WIRE_NUMBER=1560`（=四倍频后每转计数）、`Velocity_KP=12 / KI=-3.5 / KD=0`（负 Ki 与 bias=measurement-Target 方向约定自洽）。
- 教训：生成代码时**沿用每模块独立 .h 的常规组织**，把该文件的"参数集中一处"优点用 `config.h` 只放宏+extern 来实现，变量定义仍归 .c。

## 3. 五频时间片调度（TIM5 嵌套取模分频树）

- TIM5：PSC=7200/APB1DIV−1=3599、ARR=100 → 5ms 一次 `system_pulse++`（72M/3600/100）。
- ISR 内嵌套取模：`pulse%1`→Hz200（恒）、`%2`→Hz100、`%4`→Hz50、`%20`→Hz10、`%40`→Hz5、`%200`→Hz1（200 次循环重置）。`Mission.HzN` 标志位置位，主循环消费清零——**标志位型时间片**（volatile TimeSlice 结构 ✓）。
- 注释给了各档预算：200Hz≤5ms … 1Hz≤900ms。
- 主循环执行顺序 Hz1→Hz5→Hz10→Hz50→Hz100→Hz200（高频最后清，超时也不丢高频标志）。
- TIM5 抢占 0/0 最高；USART×3 均 3/3 最低——**通信让位控制**的正确方向。
- 对照：四轴 TIM1 1ms 时间片（>N 清零，周期=N+1ms）与本工程 5ms 基准+N 重置；平衡车事件驱动（DMP INT）。三种裸机调度样本技能库已集齐。

## 4. 姿态解算：Madgwick 9 轴 + 初始对准 + 增益衰减

- **DATAFUSION.c = Madgwick 2011 官方原版逐字移植**（x-io.co.uk 出处注释保留），含 `0x5f3759df` 快速逆平方根；`beta` 参数化后从 Loop 传入。
- **初始对准**（initialize.c，好实践）：加速度计 atan2 初始化 pitch/roll + 磁航向 `π-atan2(my,mx)-磁偏角` → 欧拉→四元数全套三角公式作初值 → `Convert_Quaternion_To_Euler()` 立即可用；比 (1,0,0,0) 冷启动收敛快。
- **增益自动衰减**：`beta_eff = BETA + (MadgwickGainDecendTime - MadgwickGainAutoDecend)`，3 秒内从高增益收敛到 BETA=3.5——收敛快+稳态噪声低，值得抄。
- **⚠ 欧拉角约定疑点（未定论）**：`Convert_Quaternion_To_Euler()` 里 fhi 取 roll 公式位（atan2(2q4q1+2q2q3)），但 Loop 用 `yaw=(-fhi)*180/PI - 磁偏角` 当航向、`roll=theta`、`pitch=psi`——输出映射三级错位；initialize 里 fhi 又是磁航向语义（欧拉→四元数按 ZYX yaw 位）。疑为 IMU 安装方向的软件适配或约定错误；Madgwick 反馈会拉到物理正确姿态所以"能跑"。**复用该代码前必须台架验证三轴输出与物理旋转的对应关系**。
- 传感器链：MPU6050（±250dps/±2g，gyro/(131×57.3)→rad/s、accel/16384→g）+ HMC5883L（0x3C/0x3D，ID 寄存器 0x0A='H' 探测、75Hz+8 点平均、DRDY 寄存器 0x09 bit0 轮询、**XZY 轴序读出后按 X/Z/Y 重排**——HMC5883 寄存器序即 X-Z-Y）。
- 滤波（FILTER.c）：一阶低通 `out = data*a+(1-a)*inc, a=tau/(tau+dt)`，tau=0.05；陀螺减静差（上电 100 次平均 `Caliberate_Gyro`）；罗盘减硬铁（**⚠ `Caliberate_Mag` 的 Min/Max 是写死的常量**——某台机器某次标定值硬编码，注释却写"读100个数据取平均"，换机必错）。

## 5. GPS：u-blox UBX 二进制协议（首见样本）

- **配置帧预打包常量**（CRC 已算好，直接发）：CFG-PRT 三档波特率（115200/460800/921600）、CFG-RATE 5Hz、CFG-NAV5 动态模型（0x04 车载 / 0x05 海上——**USV 应选海上**，默认配了车载，宏 `MODEL_nVEHICLE_SEA=0`）、CFG-MSG 开 NAV-PVT(0x01 0x07)/DOP/SVINFO。**不解析 Ack**（注释自认）；每次发配置前先发一个 0x0A 冗余字节"防错位"。
- 初始化序列（initialize.c）：usart3_init(115200) → CFG-PRT(115200) → usart3_init(115200) → CFG-RATE → CFG-MSG → CFG-NAV5（最终 115200；常量与注释留有 921600 旧方案痕迹）。
- **NAV-PVT 帧解析**（pvtFromGPS）：6 字节头 {0xB5,0x62,0x01,0x07,0x5c,0x00} 逐字节比对同步 → `Offset(n)` 丢弃跳过 → 小端 4 字节循环拼 int32。字段偏移表（帧头后）：fixType 20 / flags 21 / numSV 23 / lon 24 / lat 28 / hAcc 40 / velN 48 / velE 52 / velD 56 / sAcc 68 / pDOP 76。
- **DMA+IDLE 接收**：DMA1_CH3 CIRCULAR 直写 `ringBuff.Ring_Buff`；IDLE 中断里 `Length+=Tail+= RINGBUFF_LEN-CNDTR` 后 **ISR 内同步解析整帧**，再重置 CNDTR 重启 DMA。环形缓冲实际退化为"单帧暂存区"（Head=读、Tail=写与常见约定相反但自洽；每帧解析完近似归零，覆盖无害）。主循环消费路径有 `#if !DMA_RX` 条件——无双消费者竞态 ✓。
- **真 bug 三连**（详见 §9）：velN/velE 变量名对调（数据位置正确标签互换）；pDOP `/100` 写进 2 字节循环里做了两次；不校验 CK_A/CK_B（92 字节全信 6 字节头同步）。
- `SignalFromGPS()` 返回 fixtype，`!=5` 才算有效——fixtype=5 是 time-only fix，**排除正确**（好细节）。

## 6. 上位机链路：Eastar 0.2 协议 + 离线保护

**下行遥测帧 22B**（synthesis_frame，5Hz）：`0x73 0x63` + lon(4) + lat(4) + Yaw(2) + Pitch(2) + Roll(2) + hAcc(1, mm/1000 限 254→0xff) + 保留(3) + control_field(1) + sum(1)。
- control_field：bit7-6=fixtype、bit5-4=current_state；校验=[2..20] 累加和（**命名 CRC 实为加法和**）。
- 大端拆装写法正确：`(v & (0xff000000>>i*8)) >> 8*(3-i)`（对照 HLW8110 坑 74 的 &0xff00 忘移位——这是正例）。
- Pitch/Roll 带 float→u16 强转（负数变补码大值，上位机需还原）。

**上行指令帧**：头反序 `0x63 0x73` + 17B 校验区（routeToGo.yaw u16 / distance u16 / control_status u8 bit7=EndOfNav 停机 + 12B 预留）+ sum。resolve 成功→`RaspiOfflineIndicator = MotorLockTimeAfterRaspiOffline`（充值）；1Hz 递减，归零=树莓派离线→自主态刹车锁电机。
- **LoRa 模式**（WirelessPort_Online）：发送前查 AUX 脚忙闲（PD10 下拉输入），空中忙则跳过本帧；MD0=PD11 进出 AT 配置模式（ATE0 关回显找 'O' 探测在线）。
- DMA TX 单次触发式：查 TC4 标志+CNDTR==0 才 `DMA_ENABLE_ONCE(FRAMELENGTH)`，防覆盖进行中的传输 ✓。

## 7. 控制链：遥控捕获 → PID → 低有效 PWM

- **遥控 PWM 捕获**（TIM3 四通道，PA6/PA7/PB0/PB1，20µs/ARR=20）：标准双边沿状态机——上升沿记 START（STATUS|=0x4000）→切下降沿→下降沿记 END（|=0x8000 完成）→切回上升沿；溢出计数在 bit13..0，长脉宽=溢出次数×(ARR+1) 补偿。**代价：ARR=20 → 50kHz 更新中断**（每 20µs×4 通道状态判断，≈5% CPU 换 1µs 精度）。消费端 `TIM3_4channel_process()` 先停定时器+全中断再读（粗暴临界区）。
- **增量式 PID**（KP=12/KI=-3.5）：双轮速度误差**取绝对值大者**做单路控制（"夹挤"注释）——双推进电机同速+舵机转向的船艇布局，**不是差速底盘**；方向切换时 `outputPWM=0, bias=±10` 预置防反扭 ✓。限幅 ±254。
- **路径执行**（Execute_Planned_Path，10Hz）：航向差取短边（≥180 反向）→舵机三段映射（>65° 打满 / >30° 打 50° / 小角度 1.5×/4° 死区回中）；舵机实际每 3 拍=300ms 更新一次（注释写 100ms）；距离分档：>200cm 巡航 0.8×max，近距 `0.085×(distance/20)×max`——**整除使 <20cm 目标速度自然归零=停泊**（无显式刹车但等价）；EndOfNav 位→刹车。
- **低有效 PWM 约定**（TIM8 50Hz，PC6/7/8）：PWM1+高有效，但驱动板低有效输入 → `CCR=ARR`（初值）=全高=无有效脉宽=停转（"防突转"注释 ✓）；电机 `reload=400-Speed`（CCR 越大越慢）；舵机 `CCR=402-(25+inc)`——低电平脉宽=(ARR-CCR)×0.05ms=中位 1.25ms±0.75ms ✓ 舵机标准 0.5~2ms。**整套自洽，但没有任何注释点明"低有效"——读代码者必先猜 20ms 高电平怎么驱动舵机**。MOE 已开（TIM8 高级定时器）✓；CH3 漏 PreloadConfig（不一致但影响小）。
- **编码器组合测量法**（值得抄）：TIM2(右 PA0/1)/TIM4(左 PB6/7) TI12 四倍频、ARR=四倍频每转计数(1560)；溢出中断累计整圈 Round_xxx（方向用 CR1 DIR 位判回绕，换向时 Round 清零重启）；读速时**先 `TIM_ITConfig(DISABLE)` 关编码器更新中断**再读 CNT+组合 `angle=π×Round+(π/1560)×CNT`，读完重开——用关中断换原子性（但 static Round 组**忘了 volatile**，validator 抓到，见 §10）。
- `Motor_Function(wheel, Speed)`：±0.2 死区刹车；方向脚 PE0~PE3 **一次改全部 4 脚**再按 wheel 只设 CCR——左右轮方向不同时会互相覆盖；当前业务双轮永远同向（单路 PID+舵机），**latent bug 被业务约束掩盖**。

## 8. 硬件资源全表（STM32F103ZE @72MHz HSE）

| 外设 | 引脚 | 用途 | 备注 |
|---|---|---|---|
| TIM5 | 内部 | 5ms 基础脉冲 | 抢占 0/0 最高；PSC=3599 ARR=100 |
| TIM3 | PA6/PA7/PB0/PB1 | 遥控 PWM 捕获 ×4 | ARR=20（20µs），IPD 下拉；抢占 2/0 |
| TIM2 | PA0/PA1 | 右轮编码器 | TI12 四倍频 ARR=1560；抢占 1/1 |
| TIM4 | PB6/PB7 | 左轮编码器 | 同上；抢占 1/1 |
| TIM8 | PC6/PC7/PC8 | 电机×2+舵机 | 50Hz 低有效；PSC=3599 ARR=400；MOE ✓ |
| USART1 | PA9/PA10 | 树莓派/LoRa | 921600；RXNE→环形缓冲（DMA RX 自认不可用=0）；DMA TX CH4 |
| USART2 | PA2/PA3 | GY901 预留 | #if 分支未编译 |
| USART3 | PB10/PB11 | u-blox GPS | 115200；DMA RX CH3+IDLE；抢占 3/3 |
| 软 I2C | PC11(SDA)/PC12(SCL) | MPU6050+HMC5883L | 挂双器件 |
| EXTI | PC5 | 预留 | **EXTIX_Init 无人调用=死模块** |
| GPIOE | PE0~PE3 | 电机方向 | 双 H 桥 IN1/IN2 ×2 |
| GPIOE | PE5/PE6 | 双 LED | 状态/心跳翻转宏 |
| GPIOD | PD10(AUX 下拉)/PD11(MD0) | LoRa 无线串口 | 忙检测+配置模式 |
| IWDG | 内部 | ≈3.07s（PSC=7,RLR=240） | initialize 最后启动 |

死文件清单：`InputCapture/`（小写版 TIM2 捕获——uvprojx 未引用，且 TIM2 已被编码器占用，**切勿误编**）、`NRF24L01.c`（75B 空壳未编译）、`EXT/ext.c`（未调用）、`TD.c` 中 distance 先赋值后清零残留、GY901 分支整体 #if 0。

## 9. bug 清单（读码实测，按严重度）

1. **GPS velN/velE 变量名对调**（GPS.c pvtFromGPS）：偏移 48 读进 `velE`、52 读进 `velN`——数据位置正确、标签互换，用"北向速度"实拿东向。
2. **GPS pDOP 循环内双重 /100**：`for(2字节){sum; pdop/=100}` 先 lo/100 再 (+hi<<8)/100，数值必错；应在循环外除一次。
3. **帧同步匹配指针不回零**（TD resolve_frame + GPS pvtFromGPS 同款）：`if(dataIn==Header[counter1]) counter1++; else continue`——不匹配时 counter1 不清零，错过 0x63 后 0x73 仍能续上 counter1=1 造成假同步。→ 坑 77。
4. **GY901 分支灾难级贯穿**（#if 0 未编译，警戒样本）：case 0x51~0x54 无 break 且校验失败才 continue——成功路径层层贯穿，**第一帧加速度被同帧再当角速度/角度/磁场各解析一遍**（同帧 sum 必过、flag 全置位）→ return 1 时四类数据全来自单帧；且 case 0x51/0x52 内 `channelX` 被赋值三次（第三行把 Z 数据写进 X，channelZ 永不写）。
5. **硬铁校准 Min/Max 硬编码**（Caliberate_Mag）：500/-1051/626… 某台机器标定值写死进源码，注释谎称"读100个数据取平均"；换机/换安装必错。
6. **Motor_Function 整除截断映射**：`reload = 400 - (400/255)*Speed`——400/255 整数除=1（期望 1.57），占空比只在 63%~100% 区间映射，满速到不了、低速端不可达。→ 坑 78。
7. **方向脚未按轮隔离**（latent）：正/反转分支一次改 PE0~PE3 全部方向脚再按 wheel 设 CCR；当前业务双轮恒同向不触发，差速改造即爆。
8. **PID static 初值非零**：`outputPWM=130` 上电即半速输出遗留（首拍直接 130）。→ 坑 79。
9. **欧拉角三级错位疑点**（§4）：初始化 fhi=yaw 语义 vs Convert fhi=roll 公式位 vs Loop yaw=-fhi——约定混乱，Madgwick 反馈掩盖；复用前必须台架验证。
10. **UBX 无校验和**：92 字节帧不验 CK_A/CK_B 全信 6 字节头；串口误码=脏坐标直接进控制。
11. Motor.c 编码器 ISR 共享 static 组**非 volatile**（Round_Left/counterForRound_left/last_direction_left；validator 3 VIOLATION，读速时关中断只保原子性不防编译器缓存）。
12. 小项：`delay_init(u8 SYSCLK)` 收参不用；舵机 300ms 实际 vs 注释 100ms；CH3 漏 PreloadConfig；hAcc 命名 `_in_dm` 实为米；CRCvalidate[17] 注释写 16；`#define INPUT_CATPURE`（拼写）。

## 10. validator 记录与生成检查单

validator `--density hd`（F103ZE 高密度）×22 用户文件：**3 VIOLATION / 17 WARNING**，全部真实零误报——
- Motor.c FAIL：TIM4_IRQHandler 访问非 volatile static（Round_Left/counterForRound_left/last_direction_left；TIM2 侧同款结构）。
- 主要 WARNING：Loop/GPS/IMU/USART 的 ISR 内忙等/就绪等待（TC/ORE 清理类可豁免）、initialize 声明未用。
- 对照：本工程 volatile 纪律是技能库 30+ 工程里较好的（TimeSlice/四元数/欧拉角/环形缓冲全 volatile），唯 Motor.c static 组漏网——**"全局好纪律救不了局部 static 组"**的样本。

**生成此类代码的检查单**：
1. 上位机/遥控双链路失联保护：离线计数器充值制（收到帧充值、1Hz 递减、归零锁执行器），水面/空中机器人必配。
2. UBX 帧解析必须验 CK_A/CK_B（对本工程是补课）；头同步失败清零匹配指针。
3. 多字节字段换算（缩放/符号/字节序）一律在**循环拼装完成后**做，禁止循环体内做最终换算。
4. 速度/占空比映射系数用浮点或先乘后除（`arr*Speed/255`），禁止整数除法当系数。
5. PID 控制器 static/全局输出量显式清零初始化，禁止依赖上一次会话遗留值。
6. 共享给 ISR 的测量累计量（圈数/计数/标志）一律 volatile+读取侧临界区（关中断或关源）。
7. 双层架构（MCU+上位机）协议帧保留字段+累加和校验+帧头双字节；发送侧 DMA 单次触发前查 TC+CNDTR。
8. 姿态解算复用前：台架验证三轴欧拉输出与物理旋转对应（本工程欧拉约定疑点为戒）。
