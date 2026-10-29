# 多节点机器人系统（7 节点 F103/F105/F407 HAL+FreeRTOS，实测工程沉淀）

> 来源：D:\stm32project\New_rtos版本（7 个 Keil 工程，正点原子 atk HAL 模板家族；Output 均有产物=已编译，上板状态部分未知）。
> 状态：文件级读码沉淀。遥控/底盘/云台/发射为 FreeRTOS 节点，灯条与 4310 电机节点为裸机。FOC 细节见配套 foc-motor-hal.md。

## 1. 系统架构：NRF 广播 + 串口链 + 分层控制

```
                 NRF24L01 同地址同信道广播（RF_CH=40, 5B 地址全节点相同）
遥控器(F103+RTOS) ──┬──> 底盘主控(F407+RTOS)：BMI055+Mahony 1ms / 双PID / 全向轮运动学 / TIM8×4 H桥
                    ├──> 云台主控(F407+RTOS)：BMI055+Mahony / pitch PID / WS2812 / 激光
                    ├──> 灯条主控(F105 裸机)：WS2812 受击/换色灯效
                    ├──> 发射机构(F103+RTOS)：摩擦轮+拨弹 PWM（TIM2/TIM3）
                    └──> (4310 节点也同地址收)
底盘/云台 ──UART 20B 帧──> yaw_4310 / pitch_4310（F105 裸机 FOC 直驱云台电机）
4310 ──UART 回传──> 底盘/云台（帧[1-2]=电机编码器角，构成 yaw 跟随闭环）
```

- 芯片选型逻辑：F407 干 IMU+控制（168M+硬件 FPU——Mahony 浮点友好）、F105 互联型带 CAN（4310 预留总线）、F103 干简单节点。
- **NRF 五节点同地址同信道 + EN_AA 开着**：一包全收（广播），但多接收方同时回 ACK 空中碰撞——Enhanced ShockBurst 单播语义被破坏，靠 SETUP_RETR 重发掩盖（坑 58）。要做真广播应关 EN_AA/重发或按节点分 pipe 地址。

## 2. 全系统 20 字节帧协议（各节点分段填装）

| 字节 | 内容 | 填写方 → 消费方 |
|---|---|---|
| [0] | 0x55 帧头 | 遥控器 → 全部 |
| [1] | 按键位图（10 键压 8 bit，按下=0） | 遥控器 → 底盘（bit0 小陀螺/跟随、灯条 bit3 换色） |
| [2][3] | 左/右编码器 int8 | 遥控器 → 底盘（自旋速度 joy[1]×3） |
| [4][5] | 摇杆 lx 积分值（float×100 拆 int16） | 遥控器 → 底盘（yaw 目标 joy_yaw_num） |
| [6][7] | 摇杆 ly 积分值 | 遥控器 → 云台（pitch 目标） |
| [8][9] | rx 摇杆原始 −128~128 | 遥控器 → 底盘 Vx |
| [10][11] | ry 摇杆原始 | 遥控器 → 底盘 Vy |
| [12][13] | IMU 角（底盘=yaw / 云台=pitch） | 中游填 → 4310 显示/回传 |
| [14][15] | PID 输出（yaw/pitch） | 中游填 → 4310 闭环目标 |
| [16..18] | 0 填充 | — |
| [19] | 0xFF 帧尾 | — |

- 浮点编码统一 `float×100 → int16 → 2B`（范围 ±327.67，精度 0.01）；收方 `frame_sync` 20 字节环形滑窗找 0x55/0xFF 对（无校验和——错帧靠两端匹配概率过滤，**帧头帧尾同帧长滑窗对齐**是低成本同步法，正式场合加 CRC）。
- 同一套 `float_to_two_uint8_signed`/`frame_sync`/`normalizeAngleRad` 等函数在 5 个节点复制粘贴（内容一致）——多节点工程的复用靠拷贝，改协议要全节点同步改，漏一即失联。

## 3. 节点画像与任务划分

### 00.遥控器（F103C8，FreeRTOS 四任务）
- start_task（创建完自杀）→ data_get 2ms（按键+双编码器+4 路 ADC+摇杆映射±20 死区+lx/ly 积分限幅 −0.35~0.8）/ DATA_SEND 3ms（NRF 发）/ print 10ms / oled_show 100ms。
- 摇杆 ADC 通道序与逻辑轴映射错位交叉（adc_value[2]→lx、[0]→ry）——硬件布线决定，抄代码前核对原理图。

### 01.底盘主控（F407，FreeRTOS 四任务全 1ms vTaskDelayUntil；NRF 2ms）
- data_get 1ms：`BMI055_update → 度→弧度 → MahonyAHRSupdateIMU(1000Hz, twoKp=0.001"极度顺滑") → 四元数→欧拉 → process_continuous_angle(±π 回绕) → low_pass_filter(α=0.15 "解决 Kd 振动") → PID_calc`。
- data_exchange 1ms：usart_poll + 帧解算 + 模式切换（按键 bit0：小陀螺 OmniKinematics(joy_vx',joy_vy',joy[1]*3) / 跟随 OmniKinematics(rx,ry,yaw_chassis_pid 输出)）。
- **云台→底盘坐标旋转** `gimbal_to_chassis_speed_compute`：小陀螺时云台指向不变、底盘平移（cos/sin 旋转矩阵）。
- 全向轮运动学（注释自述"从麦轮改全向轮，请按注释重新接线"——底盘形制变更痕迹）：`w0=−Vx+Vy−ω(LX+LY)…` 四轮对称符号表，输出 ×data_kp(120) 直出 TIM8 CCR（H 桥=方向脚 GPIO+PWM）。
- 双 PID：yaw_gimble（小陀螺 1.0/0/0.01，反馈=IMU 连续角）/ yaw_chassis（跟随 200/0/0，反馈=4310 回传编码器角归一化）。均 PID_POSITION 型、输出限幅 300。
- motor.c TIM8 CH1-4 PC6-9：**MspInit 里 `Alternate = GPIO_AF3_TIM8` 注释【关键修复】**——F4 复用必须显式 AF 号，漏了 PWM 不出波（对照 F1 无 AF 域差异）。

### 02.云台主控（F407，与 01 镜像）
- 同 BMI055+Mahony 链；pitch_pos_pid（0.3/0/0.01，反馈 INS[1]），NRF 收 B_rx_A_buf、UART 填 [12-15] 转发 pitch 分量给 pitch_4310。
- WS2812（TIM+DMA）与 laser 目标指示；lvbo.c 滤波组。

### 03.灯条主控（F105XC 裸机）
- 主循环：key_scan + NRF 收 + 按键 bit3 切换全红/全蓝 WS2812（40ms delay 节流）；受击闪烁/流水灯/击打计数三套灯效代码整段注释（迭代遗迹）。
- `pwm_init(90-1,0)` → 800kHz 载波给 WS2812 PWM+DMA 驱动。CAN 驱动已写（can_init 存在）但 main 未启用——预留。
- 灯效更新全部 `delay_ms` 阻塞节流——裸机简单节点可接受，别抄进 RTOS 节点。

### 04.发射机构（F103+RTOS 四任务）
- Biu.c：TIM2/TIM3 各 CH3/CH4 四路 PWM（摩擦轮左右+拨弹）；`Biu_pwm_compare_set` 四路同值。
- **validator 抓到真 bug：Biu.c 的 TIM2/TIM3 无 MspInit/时钟使能**——工程里 `HAL_TIM_PWM_MspInit` 强符号在 motor.c 且只处理 TIM8，TIM2/TIM3 的时钟使能永远不会被调用（HAL 弱符号被抢占）。教训：**一个工程多处定义 HAL_TIM_PWM_MspInit 时编译器只留一个**——每个定时器分支都要在同一函数里覆盖（坑 59）。
- my_task.c：**light/motor/DATA_SEND 三任务共用 `&DATA_SEND_Handler` 句柄**（复制粘贴未改）——运行不炸（句柄没人用）但栈水位/任务管理失真（坑 60）。

### 07/08.pitch/yaw_4310（F105XC 裸机 FOC）
- 见 foc-motor-hal.md（SVPWM/AS5600/相电流/校零/闭环组合）。
- 调度：主循环 `data_change()`（串口轮询+frame_sync+velocityOpenloop(3) 调试态）；`HAL_UART_Transmit_DMA` 回传 20B 帧。

## 4. FreeRTOS 使用模式（正点原子模板家族）

- start_task 模式：`taskENTER_CRITICAL → xTaskCreate×N → vTaskDelete(自句柄) → taskEXIT_CRITICAL`——**删除自己后 taskEXIT_CRITICAL 永不执行**（任务已死）——模板级死码，临界区计数随 TCB 销毁被丢弃，调度器照常恢复；新代码直接在 main 里创建全部任务即可。
- 周期任务两种写法并存：`vTaskDelay(n)`（遥控器，漂移累积）与 `vTaskDelayUntil(&xLastWakeTime, n)`（底盘 1ms 精准）——控制环一律 DelayUntil。
- 阻塞发送在任务里：`HAL_UART_Transmit(…,1000)` 1ms 周期任务里发 20B@115200≈1.7ms——任务过载跳拍（vTaskDelayUntil 行为），改 DMA/中断发送。
- 中断全走 HAL：TIM1_UP 1ms 节拍（FreeRTOS SysTick 独立）、USART IDLE+DMA 空闲帧、NRF 收发轮询不占中断。

## 5. validator 记录（34 文件）

- FAIL 3 文件 6 VIOLATION 全真实：N04_Biu.c（TIM 时钟缺失，真硬件 bug，见 §3.04）/ N03_uart1.c（ISR 内 printf + uart1_handle/rx_buf/rx_len 非 volatile ×4，调试残留）/ N02_timer.c（ISR 访问非 volatile htim2——HAL 句柄被用户 ISR 直接触碰，规范上应 volatile 或经标志位）。
- 31 文件 PASS；WARNING：printf 重定向跨文件（6 处，fputc 在 usart.c/uart1.c 各节点内其实都有——单文件分析看不见，误报级）+ NRF 宏时钟（规则 6 宏路径正确降级）。
- `--density hd` 处理 F407/F105xC 向量域无误报。

## 6. 生成检查单（多节点/机器人类）

1. 拓扑先行：画节点图（谁发谁收、每链路的介质与帧格式），NRF 广播网要么关 ACK 纯广播、要么每节点独立地址+pipe。
2. 帧协议三件套：帧头帧尾+定长、浮点编码统一（×100 int16 两字节）、环形滑窗同步；正式加 CRC8/16。
3. 多节点共用代码（编码/滤波/运动学）抽公共头文件进仓库单点维护，禁止复制粘贴漂移。
4. HAL MspInit 归属：一工程一个 `HAL_TIM_PWM_MspInit`/`HAL_UART_MspInit`，内部 if(Instance==) 全覆盖；新定时器忘加分支=时钟无声缺失（Biu.c 实例）。
5. FreeRTOS：控制环 vTaskDelayUntil+1ms；任务句柄一一对应；阻塞 IO 算时长（115200 下 20B≈1.7ms>1ms 周期即过载）；ISR 只置标志/FromISR。
6. IMU 链固定顺序：原始值→单位转换(度→弧度！)→AHRS→欧拉→连续角(±π 回绕)→低通→PID；每步单位写注释（本工程 Get_Velocity 弧度/度混用即反例，见 foc-motor-hal.md §坑）。
7. 模式切换（小陀螺/跟随/换色）用按键位图+明确的模式枚举，切换瞬间 PID 积分要清。
8. 灯效/屏显类慢节点允许裸机+delay，但和 RTOS 节点共享代码时要检查可重入性。
