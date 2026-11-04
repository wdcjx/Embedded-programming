# 达妙电机 CAN 机器人控制（F407 + SPL + FreeRTOS + OpenOCD）

> 实测工程：f407-rod-support（推板机器人控制固件，比赛/教研场景）
> 芯片 STM32F407ZET6（uvprojx 确认）· 标准外设库 SPL · FreeRTOS V202212.01（RVDS ARM_CM4F 端口 + heap_4）
> Keil MDK 编译 + OpenOCD/CMSIS-DAP 命令行烧录（Keil 5.20 驱动认不出新 DAPLink，见 §8）
> 定位：技能库首个达妙 DM 电机样本、首个 ESP-NOW 无线串口桥样本、首个 CH9121 网口模块样本、**首个 validator 零违规工程**。
> 上板状态：README 声明已上板运行（三执行器全实测）；沉淀时未复测编译。

## 1. 架构总览

```
                ROS 上位机 (x86, udp_gateway_node)
                       │ TCP :8887
              CH9121 网口模块（USART2 PD5/PD6 @115200 透传，单连接）
                       │
        ┌──────────────┴──────────────┐
        │      STM32F407ZET6 @168MHz   │  FreeRTOS 5 任务
        │  cmd(3) motor(3) net(3,512栈)│
        │  print(2) led(2)             │
        └───┬──────────┬──────────┬───┘
       CAN1│PD0/PD1   USART3│PD8/PD9  GPIO│PE13/PE14
      1Mbps│          @115200│          │
      TJA1044│         ESP32(TX)═ESP-NOW═ESP32(RX)   DRV8251A
      STB=PC4│              │                 │        │
        达妙 DM-J4340-2EC   末端推杆          支撑杆（双向）
```

- 任务表：led(优先级2)/print(2)/cmd 命令解析(3)/motor 电机周期(3)/net 网络链路(3, 512 字栈)。
- 上电安全默认：两路 24V（ARM=PA4、DM=PA5）**断电** + 电机失能；NVIC Group_4 先设。
- 看门狗链：网络 200ms 无包→dm_stop（只触发一次）；dm_periodic 10ms 发指令维持电机"通讯在线"（达妙收不到 CAN 会自动退使能）；温度 >75℃ 状态位；ESTOP=停+失能+断 24V 三级兜底。

## 2. 达妙 DM 电机 MIT 协议全解（dm_motor.c/h）

帧格式（8 字节位域，与 ROS damiao_driver.cpp 一致）：

| 字段 | 位宽 | 量程（DM_J4340-2EC） |
|---|---|---|
| p_des | 16 | ±12.5 rad |
| v_des | 12 | ±30 rad/s |
| Kp | 12 | 0~500 |
| Kd | 12 | 0~5 |
| t_ff | 12 | ±10 N·m |

- 定点映射：`float_to_uint(x,fmin,fmax,bits)` = `(x-fmin)/(fmax-fmin) * (2^bits-1)`，超范围截断；接收侧 `uint_to_float` 对称还原。
- 下发帧 ID：MIT 模式=CAN_ID（本机 0x01）；速度模式=0x200+ID（4 字节小端 float）；位置速度=0x100+ID（pos+vel 两 float）。**电机处于哪种模式只认对应帧，其余丢弃但照样 ACK**（§4 坑 81 注）。
- 控制命令帧：使能 0xFC / 失能 0xFD / 设零 0xFE（7×0xFF + d[7]）。
- 反馈帧（ID=MasterID 默认 0）：d[0]=ID|状态<<4（1=使能 0=失能 8=超压 9=欠压 A=过流 B=MOS 过温…）；pos 16 位/vel 12 位/torque 12 位线性映射；d[6]/d[7]=MOS/线圈温度 ℃。
- 四模式状态机：IDLE（在线不发运动指令）/SPEED（kp=0+kd 纯速度）/TURNS（软位置环：误差×1.5 折算速度，限 ±5 rad/s，到位 0.05rad 停）/POS（kp=10 专用帧）。
- **实测参数**：DM_MIT_KD=1.0 稳定（**4.0 粘滑振荡**）；PI 力矩前馈 KI=1.5、限幅 ±0.8 N·m，实测 0.2rad/s 误差 75.1%→2.7%、0.8rad/s 16.6%→1.4%，退出速度模式/失能自动清积分。
- 速度斜坡软启动 DM_ACC=10 rad/s²（防反作用力矩把底盘"踢"起来）。

### 2.1 三个实战级设计（别处学不到）

1. **使能重试**：达妙上电 1~2 秒才启动，窗口内使能帧全丢（现象：命令收了、err 恒 0、电机不动）。解法：`g_enable_req && err==0 && CAN 活着` 时每 300ms 补发 0xFC，直到电机真使能；故障码/CAN 断/主动失能立即停重试。
2. **多圈累计位置**：反馈位置只有 ±12.5rad 会回绕（像时针跳回）。相邻帧差 ≥P_MAX 判回绕，`g_wrap ± 2*P_MAX` 补偿得连续多圈角度。
3. **ID 扫描双模式**：ID 未知时 0x00~0x7F 逐个发"使能+零动作帧"（MIT 全零/速度 4 字节 0），只试零动作模式（位置模式可能突然大幅动作）。**before 计数必须在发送前取**——1Mbps 下电机几微秒就回帧。命中判据=`last_fb` 变化，不是 TEC。

## 3. CAN 两颗实战金坑（can1.c 注释原文实证）

### 坑 80：NART 自动重传 × 上电无 ACK = 发送邮箱永久锁死

链条：上电默认电机断电（安全设计）→ 头几帧无 ACK → NART=DISABLE（自动重传开）的 bxCAN **硬件永远重传该帧、邮箱永不释放** → 3 邮箱全锁死 → 之后所有发送失败。实测计数 `tx=3 fail=4564 rx=0`，现象="命令收到了、电机永远零应答"。
正解：`CAN_NART=ENABLE`（失败即丢帧、邮箱立刻释放）+ `CAN_ABOM=ENABLE`（总线错误自动恢复）。100Hz 控制偶尔丢一帧无碍。
**这是"安全设计×协议特性"的组合陷阱**：单独看每个设计都对，组合起来锁死总线。

### 坑 81：SPL 的 CAN_Prescaler 语义与 TIM 相反

- TIM：写 PSC=72 → 实际 73 分频（寄存器值，硬件 +1）。
- CAN：`CAN_Init` 写 BTR.BRP 的是 (Prescaler-1)，硬件分频=BRP+1 → **真实分频系数=Prescaler 本身**（写 3 就是 3 分频）。
- 本工程位时序：PCLK1 42MHz / Prescaler 3 / (1+BS1 10+BS2 3)=14TQ → 1Mbps。作者初版写 2 → 实际 1.5Mbps，比电机快 50%，电机解不出帧零应答。
- 规矩：改 CAN 波特率先手算 `baud = PCLK1 / (Prescaler × (1+BS1+BS2))`，别拿 TIM 的 +1 直觉套。

### 附：can1.c 逐帧 ACK 结算

发送要 130µs，不在周期任务里阻塞等结果——**结算上一帧**：`can1_send()` 先查上一邮箱 `CAN_TransmitStatus`（TXOK=有对端 ACK / TERR=无人 ACK），统计 s_ack_ok/fail/pend 给 CANERR 命令。

## 4. CAN 诊断方法论（main.c 文本命令十连）

"电机一个字节都不回"的分层定位（从软件到板外逐层切分）：

| 命令 | 干什么 | 回答什么问题 |
|---|---|---|
| CANERR | ESR/TEC/REC/LEC/BOFF + ACK 统计 | TEC=0 且 rx 涨=物理层通；TEC 涨=波特率/线路不通 |
| LOOPTEST | 寄存器级切 LBKM 内部回环发帧 | 软件全对仍不通 → 问题必在板外 |
| CANSCOPE | DWT CYCCNT 临界区采样 PD0 电平（1µs=168 周期） | 眼见为实看总线波形与 ACK 位 |
| CANSNIFF | 只收不发 | 总线上有没有别人的帧 |
| PULSE | 数跳变+最短间隔 | 正常 40~60 edges / 收发器待机 0~6 |
| CANREG | 寄存器全量导出 | 远程分析（不在现场） |
| CANRST | TEC/REC 清零 | 解邮箱/错误计数卡死 |
| BAUDn | 改分频重配 | 扫波特率（2→1.5M 3→1M 6→500k…） |
| STBMODE0-4 + STBn | STB 脚五态（推挽高/低+高阻无拉/上拉/下拉） | 原理图注释不可信时全试一遍 |
| CANTEST | 200ms IO 电平统计 | 总线死活 |

方法论本质：**每条命令只切一刀，切完就知道问题在刀的哪一边**。LOOPTEST（内部回环）是最快的一刀——时钟/位时序/滤波器/中断/固件全对仍不通，问题必在收发器、线或对端。

## 5. ESP-NOW 无线推杆链路（rod_link.c）

- 链路：USART3(PD8/PD9) → 大板 ESP32(TX) ═ESP-NOW 无线══ 小板 ESP32(RX) → 推杆驱动。STM32 侧只写明文 ASCII：`forward\r\n` / `backward\r\n`，回执原样回传（`RODE` 命令查看，`ROD ECHO: [forward]`=全链路通）。
- **六组候选串口引脚**（USART3-PD8/9★原理图确认 / USART3-PB10/11 / UART4-PA0/1 / UART5-PC12/PD2 / USART6-PG14/9 / USART6-PC6/7）：`rod_link_init` 一次全初始化，RU3~RU8 运行时切换 `s_uart` 指针。这是"原理图不确定期的软件适配"模式——硬件定型后应删（六组引脚全占，加功能时必须避开）。
- RX：128B 环形缓冲 ISR 写/任务读（满丢弃+ORE 清理 ✓ NVIC 优先级 11 ✓），`rod_read_line` 行缓冲（\n 结行/\r 丢弃/空行忽略）。

## 6. 网口协议与文本/二进制共存（net_link.c + main.c）

- CH9121 = 串口转 TCP 透传模块（**单连接**：ROS 网关占着调试助手就连不上）；上行 24B `55|st|pos|vel|eff|turns|t_mos|t_rot|seq|XOR8`，下行 8B `AA|cmd|float|seq|XOR8`；默认 10Hz 上报（从 50Hz 降——串口/日志刷爆的迭代注释）。
- **TCP 流切包**：透传字节会粘连，靠帧头 0xAA 重同步，XOR 校验不过丢帧重找头，不假设"一次一包"。
- **单读者设计**（干净解决双消费者抢字节）：USART2 RX 只进 net_link_task；组包位置非 0xAA 字节 → `cmd_feed_byte()` 转交命令行。命令空闲 100ms 超时收尾（无换行也能执行）；命令归一化（大写+去空格，`ARMON`/`arm on` 等价）。
- 心跳保活：收到任何合法下行包刷新运动指令时限（x86 网关只发一条 set_speed 后靠 10Hz 心跳）；链路断→心跳停→200ms 超时停车，安全性不降。
- 发送互斥：二进制包与文本打印共用 USART2，一把互斥锁防"插花"（`usart2_tx_lock_init` 在任务创建前调）。

## 7. RTOS 纪律样本（这个工程值得抄的部分）

- FreeRTOSConfig.h：`configMAX_SYSCALL_INTERRUPT_PRIORITY=191`（优先级 11），所有用 FromISR API 的中断（CAN1_RX0/USART2/USART3）抢占优先级都=11，注释明写规则。三系统 Handler 由 `#define vPortSVCHandler SVC_Handler` 等宏映射接管，it.c 注释警告"绝对不能再定义"。
- 钩子直接 `usart_send_string` 不走锁：vAssertCalled/栈溢出钩子在调度器不可靠时还锁就死锁——细节意识。
- volatile 纪律：ISR 共享标志全 volatile（g_last_rx/g_wd_trip/s_rxbuf…），**30+ 工程里首个 validator 零 VIOLATION**。
- 上电顺序：先设 NVIC 分组再初始化外设；两路电源默认断电+失能双保险。

## 8. OpenOCD 命令行烧录（flash/ 四脚本）

- 背景：**Keil MDK 5.20 的 2016 版驱动认不出 2023 版 DAPLink 固件**（"Target DLL has been cancelled"），且 DAPLink U 盘拖拽烧录报 "Flash algorithm write verify command FAILURE"。解法：Keil 只编译，OpenOCD 烧录（自带 CMSIS-DAP v1/v2 驱动 + 自己的 stm32f4x.cfg 烧录算法，不依赖调试器固件版本）。
- flash.bat 工程化细节：OpenOCD 版本目录自动探测（`D:\tools\openocd\*` 下任意版本，升级不破坏脚本）；fromelf axf→hex 再 `program verify reset exit`；`/q` 参数供 Keil After-Build 静默调用；失败提示降 SWD 时钟（1000→500→100kHz）。
- **路径规矩（吃过亏的注释）**：`-c "program ..."` 内的路径必须 ASCII 且相对——中文绝对路径经 Windows 参数传递会损坏导致烧录失败。cmd `if (...)` 块内 echo 文本不能带圆括号（mis-parse）。
- 换调试器只改 openocd.cfg 一行（cmsis-dap/stlink/jlink），target 配置不动。

## 9. Bug/观察清单

| # | 条目 | 性质 |
|---|---|---|
| 1 | 坑 80 NART 邮箱锁死（§3） | 已修复，注释留证 |
| 2 | 坑 81 CAN_Prescaler 写 2 变 1.5Mbps（§3） | 已修复 |
| 3 | dm_motor.h 速度模式(0x200+ID) 帧注释与 MIT-only 实现并存 | 文档性：电机只认 MIT 帧，0x200 仅扫描用 |
| 4 | rod_link 六组串口引脚全占（PA0/1、PC12/PD2、PB10/11、PC6/7、PG14/9） | 待硬件定型后裁剪 |
| 5 | g_status（dm_status_t）非 volatile 但 ISR 写/任务读 | 见坑 82：回调注册式设计的静态分析盲区；M4 对齐 float 单写原子+软实时容忍，能跑，生成时应加 volatile |
| 6 | README「看门狗 200ms」与 dm.h DM_CMD_TIMEOUT_MS=200 一致 ✓ | 无问题 |
| 7 | DOC2 §5.3「USART1 可能没初始化」存疑 | usart1.c 存在且被 main 引用；文档保守表述 |

## 10. validator 记录与生成检查单

validator（--density f4，12 用户文件）：**全 PASS，0 VIOLATION / 5 WARNING**——技能库首个零违规工程。
WARNING 逐条接受理由：①NVIC 分组在 main.c 统一配置（单文件分析盲区，家族常见）；②③④⑤ usart_send_string TXE 忙等×3 处——任务上下文+互斥锁保护，发送外设必然就绪，可豁免（ISR 路径不打印）。
盲区观察：`can1_set_rx_callback(dm_can_rx_handler)` 函数指针注册使 dm_motor.c 的 g_status ISR 写入逃过「ISR 访问非 volatile」规则——**坑 82**。

### 生成检查单（达妙/CAN/FreeRTOS 组合）

1. CAN 波特率手算 `PCLK1/(PSC×(1+BS1+BS2))`，PSC 语义=分频系数本身（≠TIM 的 +1）。
2. NART=ENABLE + ABOM=ENABLE 为默认；上电无对端时自动重传会锁死邮箱。
3. 电机指令周期发送（10ms 级）维持"通讯在线"，否则达妙自动退使能。
4. 使能命令连发+周期重试（上电 1~2s 启动窗口）；disable/estop 先零力矩帧再 0xFD。
5. ISR 优先级 ≥ configMAX_SYSCALL（11）才准调 FromISR API；回调注册式设计的回调函数按 ISR 纪律写（FromISR+无阻塞+共享数据 volatile）。
6. 串口透传（网口/蓝牙）按 TCP 流处理：帧头重同步+校验，不假设包边界；单读者+转发解决多消费者。
7. OpenOCD 命令行路径全 ASCII 相对路径；中文路径必坏。
