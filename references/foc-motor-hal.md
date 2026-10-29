# FOC 无刷电机直驱（SimpleFOC 算法 HAL 移植，F105+AS5600，实测工程沉淀）

> 来源：D:\stm32project\New_rtos版本\07.pitch_4310 / 08.yaw_4310（两节点代码几乎逐字节相同，仅帧字段差异）。
> 状态：文件级读码沉淀；编译产物存在；代码处于**开环调试快照**（闭环代码齐备但被注释，主循环跑 velocityOpenloop(3)）。这是技能库首个 FOC 工程样本。

## 1. 信号链与硬件

```
AS5600 磁编(I2C 0x36, 12bit) ──机械角──> 电角度 = sensor_direction×pole_pairs×角 − zero_electric_angle
                                           │
目标角 ──> PID(位置/速度/电流 三环可组合) ──> Uq(Ud=0) ──> SVPWM 六扇区 ──> TIM1/TIM4 三相互补 PWM(中心对齐)
ADC1 4ch(PA0/1/4/5 DMA CIRCULAR) ──相电流 Ia/Ib──> cal_Iq_Id ──> 电流环
UART1 115200 ──20B 帧(frame_sync)──> 目标值；回传角度    CAN1 500kbps(PB8/9) 预留
EN: PA6(E_EN)/PB5(M_EN) 半桥使能
```

- TIM1 PA8/9/10 = 电机 1 三相；TIM4 PB6/7/8 = 电机 2 预留；`pwm_init(1440, 0)` 中心对齐 ARR=1440 → PWM 频率 = 72M/(1440×2) = **25kHz**（中心对齐记得 ×2）。
- **无死区/刹车配置**：全工程没有 `HAL_TIMEx_ConfigBreakDeadTime`——分立半桥驱动若无硬件死区会直通（坑 61）。SVPWM 互补输出的正确姿势：中心对齐+死区 500ns~1µs+MOE。
- AS5600：写 0x07/0x08=0（慢速滤波默认），读 0x0C/0x0D 拼 12bit；0.08789°/LSB。

## 2. SVPWM 实现（FOC.c，SimpleFOC setPhaseVoltage 移植）

```c
// Ud=0 时：Uout = Uq/Vcc；angle_el += π/2   （有 Ud 时矢量合成+atan2）
Uout = clamp(Uout, ±0.577)                 // 1/√3 线性调制区上限
sector = angle_el/(π/3) + 1                // 六扇区
T1 = √3·sin(sector·π/3 − angle_el)·Uout    // 作用时间
T2 = √3·sin(angle_el − (sector−1)·π/3)·Uout
T0 = 1 − T1 − T2
// 六扇区各自 Ta/Tb/Tc = … + T0/2 组合（扇区表）
motor1_pwm_set(Ta×1440, Tb×1440, Tc×1440)  // 直接写 CCR
```
- 精华：扇区表（case 1~6 的 Ta/Tb/Tc 组合）与 T1/T2 正弦公式——照抄可用；调制比上限 0.577 防过调制。
- 每次输出都重跑 3×`HAL_TIM_PWM_Start`（冗余无害，启动后 Start 幂等）。

## 3. 校零流程（Motor_init，上电标定 zero_electric_angle）

```c
1) Uq=2.5V 开环电压转一圈：angle = 3π/2 + 2π·i/500（i:0→500），每步 2ms ——拖动转子对齐
2) 反向转回（i:500→0）
3) setPhaseVoltage(3, 0, 3π/2) 锁定电角 3π/2，停 300ms ——转子吸到零位
4) zero_electric_angle = _electricalAngle() ——此时读 AS5600 即零电角
```
- 上电校准优于硬编码（工程里注释掉的 `2.297887f` 是曾硬编码的痕迹）；带载/减速比场景校零时电机会抖动——正常。
- 参数：`sensor_direction=1, pole_pairs=14, voltage_power_supply=11.7`（**极对数必须实测**：手动转一圈数磁阻台阶数，或开环转一圈数 N 次锁定）。

## 4. 三环 PID（SimpleFOC PID 类移植）+ 低通

```c
// Pid.c：梯形积分 + 双限幅
integral = integral_prev + I·Ts·(err+err_prev)/2;  integral = ±limit
derivative = D·(err−err_prev)/Ts
output = P·err + integral + derivative;  output = ±limit
// Ts 用 SysTick->VAL 24bit 向下计数差（/9e6），回绕分支+异常值兜底 1e-3
```
- 闭环组合六种：Position/Velocity/Current 单环 + Pos_Vel / Vel_Cur / Pos_Cur 级联（`Position_VelocityCloseloop`：位置环输出当速度目标）。
- 实测参数快照：Pos 1.0/0/0（limit 62.8=2π×10）、Vel 0.01/0/0（limit 60）、Cur 0.5/0/0（limit 6）——**全纯 P，调试早期**。
- Lowpass.c：一阶 `α=Tf/(Tf+Ts)`，current Tf=0.05 / velocity Tf=0.01。

## 5. 电流采样（adc.c，Inline 相电流）

```c
volts_to_amps_ratio = 1/R_shunt(0.01Ω)/amp_gain(50) = 2 A/V
gain_b = -1（B 相运放反向，硬件接法决定）
上电校零：1000 次平均 offset_ia/offset_ib（delay_ms(1) 间隔）
current_a = (V_ch0 − offset_ia)×gain_a; current_b = (V_ch1 − offset_ib)×gain_b
cal_Iq_Id：Clarke(Iα=Ia, Iβ=Ia/√3+2Ib/√3) → 只取幅值+符号近似（不做完整 Park）
```
- `cal_Iq_Id` 是简化版：不输出 Id、符号用 (Iβ·cosθ−Iα·sinθ) 判——够电流限幅用，做力矩控制要补完整 Park。
- ADC 连续模式 4 通道 DMA CIRCULAR（PA0/1/4/5）——非中心点同步采样，电流纹波混入；正式 FOC 用 TIM1 CC4 触发在 PWM 中点采样。

## 6. 单位混乱实录（本工程最大坑，坑 62）

- `as5600_read_angal()` 返回 **raw count**（0~4095），换算角度的行被注释——`Get_Angel_L` 里 `/4096×2π` 出**弧度**；
- `Get_Velocity_H` 的回绕判断却按**度**写：`angle_diff > 180 → −360`——而输入是弧度（|diff|≤π<180 永不触发，靠 Get_Angel_L 的圈数跟踪兜底才没炸）；
- PID 误差又 `×180/PI` 转度；`Get_Angel_Notrack` 用 0.08789（度/LSB）×π/180 出弧度。
- 规矩：**每个函数注释标注返回单位（count/度/弧度/°·s⁻¹）**，接口统一一种（SI 弧度），转换只在边界做一次。

## 7. CAN（F105 互联型，can.c）

- 500kbps：APB1 36M / Prescaler 4 = 9M / (1+BS1 9TQ+BS2 8TQ)=18TQ → 500k ✓。
- 过滤器：32bit 掩码全 0=全收，FIFO0，FilterBank 0；**SlaveStartFilterBank=14**（双 CAN 才有意义，单 CAN 无害）。
- `can_send_data`：ExtId+IDE_EXT；**发完 `while(邮箱!=3空) 死等无超时`+printf 调试残留**（坑 45 家族）；AutoRetransmission=DISABLE。
- MspInit PB9 AF_PP TX / PB8 AF_INPUT RX，上拉。

## 8. validator 与 bug 清单

- N07/N08 全部 10 文件 PASS（0 VIOLATION）；WARNING：printf 重定向跨文件（fputc 实在 uart1.c，单文件分析不可见）。
- 人工发现（validator 盲区）：①无死区配置（§1）②弧度/度混用（§6）③can_send 死等+ISR printf 风格 ④Motor_init 校零里 `setPhaseVoltage(0,0,angle)` 传的是循环残留 angle（无害但不严谨）⑤closed-loop 全注释的调试快照状态。
- 07 与 08 文件级 diff≈0（仅帧字段注释），共用驱动的双节点应抽出公共库。

## 9. 生成检查单（FOC 类）

1. 极对数/传感器方向/供电电压三参数上电打印确认；校零流程=开环拖动→锁定 3π/2→读角。
2. 互补 PWM：中心对齐 + HAL_TIMEx_ConfigBreakDeadTime（0.5~1µs）+ MOE——三者缺一直通风险。
3. 电角度 = (方向×极对数×机械角 − 零电角) 归一化 [0,2π)。
4. SVPWM：Uout 限 0.577、六扇区表、T0/2 补底；ARR 与调制比联动（CCR=T×ARR）。
5. 三环 PID：梯形积分+限幅；Ts 取真实间隔（SysTick 差分+回绕）；级联=外环输出进内环目标。
6. 电流采样：R+增益换算、上电校零 1000 均值、B 相反向写 gain=-1；同步采样用 TIM CC4 中点触发。
7. 单位纪律：接口统一弧度，边界转换，注释标注每函数单位。
8. 闭环前先开环：velocityOpenloop 验证 SVPWM/驱动/编码器方向，再逐环闭合（本工程快照即停在这一步）。
