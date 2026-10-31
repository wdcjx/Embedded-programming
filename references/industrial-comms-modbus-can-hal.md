# 工业通信与计量（Modbus RTU / CAN / RS485 / HLW8110）+ 合集杂项

> 源工程：`D:\stm32project\arm32_open-master\arm32_open-master`（26 子工程合集）。全 HAL 轨（CubeMX），F103RC/C8 + F407 + MM32F3273。
> 状态：**文件级读码**，未上板未编译。FreeModbus 移植层与 CAN 收发为技能库首见样本。

## 1. 合集总览（26 子工程价值评级）

| 子工程 | 芯片 | 内容 | 技能库价值 |
|---|---|---|---|
| STM32F103_Project_FreeModbus | F103RC | **FreeModbus RTU 从机移植** | ★★★ 首见 |
| STM32F103_CAN_Project | F103RC | CAN 收发（F1 bxCAN） | ★★★ 首见 |
| STM32F407_CAN_Project | F407VETx | CAN 同款（F4） | ★（与 F1 同构） |
| Rs485AndCanBoard-V1 | F103C8 | RS485+CAN 板 bring-up 模板（主循环空） | ★ 引脚表 |
| STM32F407_HAL_HLW8110 - 3-28 | F407 | **HLW8110 计量配电单元**（Modbus 参数化+IAP 从机） | ★★（驱动源码缺失，见 §8） |
| stm32f103rc_rc522 | F103RC | RC522 RFID（淘宝驱动+WS2812 蜂鸣） | ★★ 首见 |
| stm32f103rct6_ws2812b / w282xb | F103RC | **WS2812B PWM+DMA 双缓冲** | ★★ 精彩实现 |
| stm32f407_coremark | F407ZETx | CoreMark 官方 barebones 移植 | ★ |
| auto_init_stm32f4_hal | F407VETx | bsp 自动初始化框架 | 未深读 |
| 02-中景园LCD_2.8 / filter / DAC 噪声 / sg90 / sw_iic / HUMI_TEMP / timeslice / IAP Test | F103/F407 | 与既有文档重合（LCD/SPL 滤波/DAC/舵机/软 I2C/传感器/时间片/IAP） | 跳过 |
| MM32 中景园屏 ×3 | MM32F3273G8P | 国产芯+屏例程 | ★ 立场注记 |
| hi3863 / ymodem_tool / 文档模板 / HLW8112 资料包 | — | 海思 WiFi SoC（超范围）/PC 上位机/模板/PDF 资料 | 边界声明 |

## 2. FreeModbus RTU 从机移植（首见，重点）

### 2.1 架构与三件套
```
main: eMBInit(MB_RTU, 0x01, 3, 9600, MB_PAR_NONE)  // 模式/从机地址/端口3/波特率/校验
      eMBEnable()
loop: eMBPoll()                                    // 轮询驱动协议栈（裸机 BARE 版核心契约）
```
- BARE 移植 = 库不动 + **4 个 port 文件**：`portserial.c`（UART）、`porttimer.c`（3.5 字符帧超时）、`portevent.c`（EV_EXEC 事件轮询）、`port.c`（用户寄存器回调）。

### 2.2 portserial.c 要点（huart3 硬编码）
- `vMBPortSerialEnable(RxEn, TxEn)`：RXNE/TXE 中断逐个开关——**协议栈全权决定收发时机**，发送也走中断逐字节（`pxMBFrameCBTransmitterEmpty`→`xMBPortSerialPutByte`）。
- 校验映射：MB_PAR_EVEN/ODD→`UART_WORDLENGTH_9B`（带校验 9 位帧），NONE→8B——Modbus 偶校验默认要求 11 位帧（1 起+8 数据+1 校验+1 停）。
- `PutByte/GetByte` 直写 `USART3->DR`（不走 HAL API，快路径）。
- **注释里写了 485 方向钩子位但未实现**（见坑 75）。
- it.c 接线：`USART3_IRQHandler` USER 区调 `USART3_My_Callback()`（查 RXNE/TXE 标志→清→通知协议栈），**不进 HAL_UART_IRQHandler**——移植者绕开 HAL 状态机直接管标志，与 HAL 收发 API 不共存。

### 2.3 porttimer.c 要点（htim4）
- `PSC=3599`（72MHz/72/…实际 72MHz/(3599+1)=20kHz=**50µs 粒度**），`Period=usTim1Timerout50us-1`——库按波特率算好 3.5 字符时间传入。
- `vMBPortTimersEnable`：清计数器+使能（每次帧间静默重新计时）；溢出中断→`pxMBPortCBTimerExpired`→RTU 判帧结束。
- it.c：`TIM4_IRQHandler` 先调 `TIM4_My_Callback()` 再 `HAL_TIM_IRQHandler`。

### 2.4 port.c 用户寄存器层（demo 可直接当模板）
四类映射各一函数，全部：`usRegIndex = usAddress - 1`（Modbus 地址 1 起）→越界查 `(usRegIndex + usNRegs) > SIZE` → 大端装填（`>>8` 高字节先发）。线圈/离散量按 8 位一组打包。**寄存器数组就是用户与协议栈的全部界面**：业务侧只写数组，Modbus 侧自动可见。

## 3. CAN 收发（F1 bxCAN，首见）

- 位时序：APB1 36MHz，`PSC=4, SJW=1, BS1=9, BS2=8` → 36M/4/(1+9+8)=**500kbps**（常用计算式：baud = PCLK1/PSC/(1+BS1+BS2)）。
- 过滤器全通过：32 位 IDMASK 模式 ID=0 Mask=0 → 不过滤（调试期标配；量产按 StdId 分组）。
- **F1 特有**：CAN_RX（PA11）配 `GPIO_MODE_INPUT + PULLUP`（F1 无 AF 选择寄存器，CAN 收脚必须上拉输入——工程注释"CubeMX 配 CAN 需上拉否则初始化失败"）；中断号 `USB_LP_CAN1_RX0_IRQn`（**F1 的 USB 低优先级与 CAN1_RX0 共用向量 20**，二选一外设）。
- 收：`HAL_CAN_ActivateNotification(CAN_IT_RX_FIFO0_MSG_PENDING)` → 弱回调 `HAL_CAN_RxFifo0MsgPendingCallback` → `HAL_CAN_GetRxMessage(FIFO0)`。
- 发：`HAL_CAN_AddTxMessage`（空邮箱自动分配 + `TransmitFifoPriority=ENABLE` 按 FIFO 而非 ID 优先）。
- `AutoRetransmission=DISABLE`：发送失败（仲裁丢/错误）不自动重发——**调试方便但工业部署应 ENABLE**，否则丢帧无感知。

## 4. HLW8110 智能配电单元（架构实录，驱动缺失）

- F407@168MHz（HSE 8M/4×168/2）、`SCB->VTOR = FLASH_BASE | 0x10000`（**IAP 从机，Boot 占前 64KB**——与坑 64 VTOR/IROM1 同步规矩呼应）。
- 四串口分工：USART1=**FreeModbus 从机**（地址/波特率/校验**从 24C02 EEPROM 读出参数化**——Modbus 从机可现场改配置的工业标配做法）、USART2=9600 **9 位偶校验**（HLW8110 计量 UART 协议特征）、UART4=DMA CIRCULAR 收、USART3=8N1。
- 24C02 参数表：地址/波特率/校验/交压上下限 25V~21V（×100 存两字节）/流上限/延时开/温度上限 45°/电池 12~25V/电能累计（两字节）。
- 主循环轮询全家桶：Key_Poll/Calculate_HLW8110_Poll/eMBPoll/Battery_Poll/Beep_Poll/ACDC_Relay_Poll + IWDG 喂狗 + 500 拍温度 + 101 拍 DS1302 时钟显示。
- 交流失电检测（PF11）→ 关双继电器 + 电能累计写 EEPROM。

## 5. RC522 RFID（淘宝驱动标准结构）

- 流程：`PCD_Request(REQALL/REQIDL)` 寻卡（返回卡类型：0x0400=S50、0x0200=S70、0x4400=UltraLight）→ `PCD_Anticoll`（防冲突取 4 字节 UID + **XOR 校验第 5 字节**）→ `PCD_Select` → `PCD_Halt`（休眠防重复读）；认证/读写块被注释（读卡号门禁够用）。
- 复位序列：硬复位 RST 脚三段 + 软复位 0x0F + TMode 0x8D/TReload 30/CRC 0x6363 标准初始化。
- **移植适配手法实录**：`rc522.h` 里 `#define osDelay HAL_Delay`——淘宝驱动源自 RTOS 工程，裸机移植用宏替换延迟函数（快而不脏的适配）。

## 6. WS2812B PWM+DMA 双缓冲（精彩实现，Bsp/ws281x.c 99 行全量）

- 原理：TIM PWM 800kHz 级，**每个 bit = 一个 CCR 装载值**（Code_1≈0.8µs 高 / Code_0≈0.4µs 高），24 bit 灯珠=25 字缓冲（末位 0 兜底）；`WS2812_Rst[240]` 全零数组=帧尾复位码（240×1.47µs≈350µs>50µs 规范）。
- 双缓冲乒乓：`HAL_TIM_PWM_PulseFinishedCallback`（DMA 完成）里当前缓冲在发时，**预解码下一颗灯颜色进另一缓冲**，`ins` 标志交替——CPU 每灯只解一次码，DMA 链式零 CPU 干预。
- 颜色重排：uint32 0x00RRGGBB → `(G<<16)|(R<<8)|B`（WS2812 是 **GRB** 序，驱动必改）。
- 本工程 ARR=105→679kHz（周期 1.47µs），在 WS2812B ±600ns 容差内合法。
- 数据结构：`WS2812_Data[30]` 颜色数组（业务界面）+ SendBuf0/1[25]（编码后 PWM 值）。

## 7. CoreMark 移植（stm32f407_coremark）

- 官方 coremark-main barebones 包整体进工程，main 里 `core_main()` 直调（printf 重定向到 USART1 出分）。
- HSE **25MHz** 晶振时钟树（/25×336/2=168MHz）——25M 是 F407 常见替代晶振，PLL 参数与 8M 版完全不同。

## 8. bug/瑕疵清单（按严重度）

1. **HLW 电能高字节截断（真 bug）**：`Rom_24CXX_Wirte_OneByte(F_AC_E_VALUE_H, (uint16_t)(E*100) & 0xff00)`——与 0xff00 相与后**未右移 8 位**，uint8_t 形参截断后高字节恒存 0x00（低字节行 `& 0xff` 正确）→ 电能累计断电必丢高半。多字节拆装规矩：`(v>>8)&0xff` 与 `v&0xff` 成对出现（坑 74）。
2. **FreeModbus RS485 方向钩子未实现**：`vMBPortSerialEnable` 的 Tx 分支只开关 TXE 中断，485 收发方向脚没切——接 RS485 收发器时**发送期仍是接收态**，总线撞车（坑 75；纯 RS232/TTL 直连无此问题）。
3. CAN1 工程在 `HAL_CAN_RxFifo0MsgPendingCallback`（中断上下文）里刷 LCD（`LCD_Clear_H`+`LCD_ShowNumArray`，并口 FSMC 全屏操作几十 µs~ms 级）——坑 68 家族第 N 例，ISR 只置标志的整改路径同桌宠 B 版。
4. HLW8110 工程仓库**不完整**：main.c include 的 hlw8110.c/mygpio/acdc_relay/battery/mb 封装/ds18b20/24cxx/oled/ds1302/GK801 共 11 个驱动文件不在仓库（只传了 CubeMX 生成层 .ioc+Core+Drivers），**本仓库状态编译不过**——开源工程先验完整性。
5. FreeModbus demo 注释漂移小例：`eMBInit(..., MB_PAR_NONE)` 实参无校验，行尾注释写"奇校验"。
6. CAN1 `AutoBusOff=DISABLE`+`AutoRetransmission=DISABLE`：总线关闭不自恢复+丢帧不重发，演示工程无妨，工业模板须双 ENABLE。

## 9. validator 记录

21 用户文件（--density md 18 + f4 3）：**3 VIOLATION 全真实零误报**——A32_CAN1_can.c ×2（RxData/RxHeader 非 volatile，CAN 回调=中断上下文）、A32_WS_main.c ×1（max4466_value，ADC 回调）。FreeModbus 四 port 文件全 PASS（PutByte 直写 DR/My_Callback 自查标志——不落 HAL 回调规则，无volatile 需求）。

## 10. 生成检查单（Agent 产出此类代码时自查）

1. Modbus 从机五件套：4 个 port 文件职责不混（serial/timer/event/reg）；`eMBPoll()` 必须在主循环高频调用；寄存器数组是唯一业务界面。
2. RS485 半双工必做方向切换：Tx 使能→DE=1，末字节发完（TC 中断而非 TXE）→DE=0；无 DE 脚的 TTL 直连可免。
3. 3.5 字符超时定时器粒度=50µs（PSC 按 fCLK/20k 算）；重使能前清计数器。
4. CAN 五件套：位时序公式 baud=PCLK/(PSC×(1+BS1+BS2))；F1 收脚上拉输入；过滤器至少一条激活；FIFO0 中断挂 `USB_LP_CAN1_RX0_IRQn`；AutoBusOff/AutoRetransmission 工业双 ENABLE。
5. WS2812B 模板：bit→CCR 数组 + 双缓冲乒乓 + 帧尾 Rst 数组 + GRB 重排；灯数多时 RAM=25×4B×2。
6. 多字节入 EEPROM/寄存器：高低字节拆装 `(v>>8)&0xff`/`v&0xff` 成对，写完读回校验。
7. IAP 从机（VTOR 偏移）+ 参数 EEPROM + Modbus 参数化三件套是"智能配电单元"标准架构，生成此类产品代码按此分层。
