# 智能防疫门禁系统（MLX90614 测温门禁）— HAL 轨工程笔记

> 来源：课设-智能防疫门禁系统（用户提供，STM32F103C6Tx + CubeMX HAL + Keil V5.32，CubeMX 6.4.0 + FW_F1 1.8.4）。
> 定位：技能库**最小目标芯片**（F103C6：32KB Flash / 10KB RAM）样本 + MLX90614 非接触红外测温样本 + 课设级系统集成范例。
> 状态：文件级读码+validator 实测；上板状态未与用户确认，按未上板对待。

## 1. 业务闭环（门禁判定链）

```
主循环（无 RTOS、无节拍调度，纯大循环）：
  MLX90614 读温 +2℃ 硬校准偏移
  ├─ temp < 33            → 无人在测温区，跳过（LED 心跳闪烁）
  ├─ 33 ≤ temp < 37.4     → 正常：USART1 上报编码温度、LCD 绿显「正常」、
  │                          人数++、舵机 135° 开门 → HAL_Delay(3000) → 45° 关门
  └─ 37.4 ≤ temp ≤ 45     → 异常：LCD 红显「注意异常」、error_flag=1、
                             while(error_flag){语音播报; 蜂鸣;} 死循环
                             → 只能按 k1（EXTI 清 error_flag）人工解除 → 重绘界面
TIM2 500ms 回调：error_flag 期间翻转 beep（报警节拍，与语音死循环并行发声）
```

- 上报编码：`temper = 10*temp − 256`（37.4→118、45→194），单字节阻塞发送 `HAL_UART_Transmit(&huart1,...,100)`。
- 异常闸机逻辑（异常必须人工确认才能恢复通行）是门禁类产品的正确形态；用 `while(error_flag)` 阻塞实现是课设级写法，产品级应状态机化。

## 2. 硬件资源表（.ioc 声明 vs 代码实际 —— 存在漂移，见坑 72）

| 功能 | .ioc 声明 | main.h/gpio.c 实际 | 备注 |
|---|---|---|---|
| MLX90614 SMBus | sda=PB13 / scl=PB14 | 同 | 软件 SMBus，推挽+切输入 |
| 语音 data | data=PB10 输出 | 同 | 一线串口（半成品，见 §5） |
| 语音 busy | **busy=PB2 输入** | **busy=PB1 输入** | ⚠ 漂移 |
| 语音 data1 | （不存在） | **data1=PB11 输出** | ⚠ .ioc 无此脚 |
| LCD ST7735 1.8" | cs=PB3/sck=PB4/sda=PB5/rs=PB6/rst=PB7 | 同 | 软件 SPI（QDtech 驱动） |
| 舵机 SG90 | TIM1_CH1 PA8，PSC=7199/ARR=199 → 50Hz | 同 | CCR=angle/9+5（0.5~2.5ms） |
| 蜂鸣器 | beep=PB15 推挽 | 同 | 有源，电平驱动 |
| LED | led=PC13 | 同 | 心跳+按键反馈 |
| 4 键 | k1~k4=PA4~PA7 EXTI 下降沿+上拉 | 同 | k1 解除报警/k2 手动开门/k3k4 占位 |
| 节拍 | TIM2 PSC=7199/ARR=4999 → 500ms 中断 | 同 | 报警蜂鸣节拍 |
| 双串口 | USART1 PA9/10 115200（上报）；USART2 PA2/3 115200 + DMA1_CH6 RX | 同 | **USART2+DMA 全套初始化但业务零调用（死外设）** |
| 时钟 | HSE 8M ×PLL9=72MHz，APB1÷2 | 同 | NVIC Group_4 全抢占；EXTI/USART=1，TIM2=2 |

启动序列注意：`HAL_TIM_Base_Start_IT(&htim2)` 前先 `__HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE)` 清残留更新标志——防御性写法，值得沿用。

## 3. MLX90614 驱动（SMBus，技能库首例）

通信层：软件 SMBus on GPIO（SCL 推挽常驱、SDA 推挽输出/上拉输入切换）。

- 位时序：`delay_weimiao(n)` = 空循环 18n 周期 @72MHz ≈ 0.25µs/单位；start/stop 用 5 单位（1.25µs）——低于 SMBus 规范最小 1.3µs 但 MLX90614 实测容忍。**延时常量按 72MHz 烧死**，换时钟源必坏（坑 55 家族）。
- ACK 等待：250 次超时保护 ✓（读 ACK 前先把 SDA 切输入）。
- 读 RAM 温度帧（SMBus Read Word）：
  `Start → (saddr<<1)|W → cmd(RAM|0x07) → Restart → (saddr<<1)|R → DataL → DataH → PEC → Stop`
- 温度换算：`temp = raw * 0.02 − 273.15`（OBJ1=RAM 0x07；环境温度=0x06；0.02K/LSB）。
- PEC：CRC-8 多项式 0x107（x⁸+x²+x+1），驱动用 48 位移位除法实现（pec_cal），参与字节 = `[0, DataH, DataL, SA|R, cmd, SA<<1]` 逆序 6 字节。
- 附加能力（驱动已带、本工程未用）：PWM→SMBus 模式切换（SCL 拉低 >1.44ms）、睡眠退出（SCL 高时 SDA 拉低 33ms + 等 250ms）、读/写 EEPROM（写前必须先擦 0、擦后等 5ms）、改 SMBus 地址（写 EEPROM 0x0E，函数体已注释成空壳）。
- 重试结构：NACK/PEC 失败重试 10 次，`goto stop_xx` 标签收尾——可用但老派。

驱动移植残骸（SPL→HAL 半移植，文件头留淘宝店联系方式）：
- `mlx90614.h` 残留 SPL 位带宏 `PBout(14)/PBin(13)` 与 `RCC_APB2Periph_GPIOB`（HAL 工程不存在，仅占位）；
- 头文件声明 `void delay_usssss(int n);` 实现已改名 `delay_weimiao`（死原型）；
- `mlx90614_init()` 里 `GPIO_InitStruct.Pin = MLX90614_IIC_PIN_SCL|MLX90614_IIC_PIN_SCL`（SCL 写两遍、SDA 漏配）——被 CubeMX 的 gpio.c 启动初始化掩盖才没炸。
**规矩：引入淘宝/网盘驱动先跑一遍「死原型/SPL 宏残留/初始化脚位 diff」三查。**

## 4. 执行器层

- 舵机：`CCR = angle/9 + 5`（0°→0.5ms、90°→1.5ms、180°→2.5ms，50Hz/ARR=199）。与 peripheral-recipes 舵机配方一致；注意 main 注释「调到45度」实际代码是 90°（注释漂移，代码为准）。
- 蜂鸣：有源蜂鸣器电平驱动，TIM2 500ms 翻转产生「滴—滴—」报警节拍；k1 解除时 `beep_Pin SET` 停止（依赖 PB15 下拉默认低，置高即停声——**有源蜂鸣器高电平响还是低电平响取决于模块，读 .ioc 的 PinState=RESET 初始意图**）。
- 语音（一线串口，半成品现场）：`yuyin(data)` 开头 `while(busy==0) HAL_Delay(1)` **死等无超时**（坑 45 家族）；`data&0x01` 分支只把 data 拉低不复位（置位代码被注释）；else 分支循环写的是 **data1_Pin（PB11，.ioc 里不存在的脚）**；大片注释残骸。功能上「能出声」靠的是 else 分支 10×180ms 的拉低脉冲序列恰好类似某种单线触发协议。**结论：语音链路按未完成对待，不可复用。**

## 5. 显示层（ST7735 + QDtech GUI）

- ST7735S 1.8" 128×160（`LCD_W=128`，USE_HORIZONTAL=2 即 180° 旋转），QDtech/lcdwiki 家族驱动（lcd.c 12KB 完整 init 序列：0x11/B1~B4 帧率/C0~C5 电源/gamma E0-E1/0xF0/0xF6/0x3A=0x05/0x29）。
- 软件 SPI `SPIv_WriteData`：MSB first，SCK 低-高打拍（QDtech SPI.c 全家通用）。
- 中文字显：GUI_DrawFont16 逐字取模（FONT.H 45KB 字库，16×16），每个汉字一次函数调用——课设级排版（x 坐标手写递增 16）。

## 6. Bug 清单（读码实测）

1. **PEC 校验永远失败**：`mlx90614_read_ram()` 计算缓冲 `buf[4]=EEPROM|cmd`（0x20|cmd）但交易实际发送 `RAM|cmd`（0x00|cmd）——命令前缀用错，PEC 恒不匹配 → 10 次重试全耗尽（每次读温 10 连发 SMBus 交易）→ **照样返回最后一次数据**。校验形同虚设+性能暗损。对照：read_eeprom 的 buf[4] 是对的（复制来源），复制到 read_ram 忘改。→ 坑 73。
2. **.ioc 与生成代码漂移**：busy=PB2(.ioc) vs PB1(代码)；data1=PB11 只存在于代码。按 .ioc 接线会接错脚。→ 坑 72。
3. **error_flag 非 volatile**（validator 2 VIOLATION）：EXTI/TIM2 回调写、主循环 while 读，开优化必死循环。
4. mlx90614_init SCL|SCL 重复、SDA 漏配（被启动初始化掩盖）。
5. yuyin() 语音半成品（见 §4）：busy 死等、data/data1 双线混乱、拉低不复位。
6. 死外设：USART2+DMA1_CH6 全套初始化+中断使能，业务零调用（砍需求残留）。
7. 注释漂移：`Servo_Control(90)` 注释「调到45度」；`(uint8_t)10*temp-256` 写法迷惑但结果正确（cast 只作用于 10）。
8. 死码：`PEC=PEC+1`（读 RAM 尾部）、`Float_To_Int/transunion`（定义未用）、`mlx90614_set_addr` 空壳、`Test_Color` 测试残留。

## 7. validator 记录 + 生成检查单

- validator（--density md，F103C6 LD 向量与 md 表兼容，加热台同款先例）：11 用户文件，2 VIOLATION（main.c error_flag ×2）全真实零误报，其余全 PASS。
- 生成「测温门禁」类代码的检查单：
  1. 阈值判定链给完整三分支（无效驻留/正常/异常），异常态必须有人工解除路径（按键/超时二选一）；
  2. ISR/回调共享标志一律 volatile + 主循环消费（validator 会抓）；
  3. 开门阻塞用 `HAL_Delay` 前先确认测温/报警允许被阻塞；产品级改状态机（open→hold→close 计时推进）；
  4. MLX90614：PEC 缓冲与实际发送字节逐一 diff；重试耗尽要上报错误码而不是返回旧数据；
  5. 舵机角度→CCR 换算与 ARR/PSC 一起交付（公式=angle/9+5 仅在 50Hz/ARR=199 成立）；
  6. 砍需求后删干净外设初始化（死外设=误接线+Flash 浪费）；
  7. 拿淘宝驱动先跑「死原型/SPL 宏/初始化脚位」三查再合入。
