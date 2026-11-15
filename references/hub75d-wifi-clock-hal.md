# HUB75D 点阵 WiFi 时钟（STM32F103C8T6 + FreeRTOS + ESP8266 双源校时 + OTA）

> 样本：led32x64_wifi_clock（作者开源工程，F103C8T6 + FreeRTOS CMSIS_V2 + CubeMX HAL，HSE 8M×9=72MHz + LSE RTC）。
> 32×64 HUB75D RGB 点阵时钟：WiFi/GPS 双源校时 + 万年历农历 + 温湿度 + PIR 人体感应息屏 + 双分区 OTA（boot + YMODEM）。
> 定位：**点阵显示驱动（DMA-ODR 方案）+ ESP8266 AT 表驱动 + 掉电安全 OTA + 任务组省电**四个范式的实测参考。
> 实测状态：作者实测上板运行；本技能按读码沉淀，未重新上板。

## 1. 工程总览（三工程合一）

- `stm32f103c8t6/`：主固件（App，0x08002000 起，54.5KB 上限）。
- `stm32f103c8t6/boot/`：独立 Bootloader 工程（0x08000000~0x08001BFF 共 7KB，末页 0x08001C00 = OTA 参数页）。
- `esp8266_at_web_config/`：**空目录**——readme 声称的 ESP Web 配网固件与 `auto_build/` 三工具链构建脚本均不在仓库里（readme 描述 ≠ 仓库内容，坑 46 家族；评估此类工程以代码为准）。
- 调试：SEGGER RTT（trace.c 三通道日志，见 §7）。

## 2. 引脚资源表（以 app.ioc 为权威）

| 引脚 | 功能 | 说明 |
|---|---|---|
| PA0 | PIR_INT | EXTI0 人体感应唤醒（HAL_GPIO_EXTI_Callback→DISP 事件） |
| PA1 | DISP_POWER | 点阵电源开关（息屏断电） |
| PA2/PA3 | USART2 TX/RX | GPS NMEA 输入 |
| PA4 | HUB_B2 | 下半屏蓝（**软件旧版宏错写 PB4，见坑 89**） |
| PA5/PA6/PA7 | HUB_G1/R1/B1 | 上半屏 RGB |
| PA8 | HUB_D | 行地址 bit3（1/16 扫描） |
| PA9/PA10 | USART1 TX/RX | ESP8266 AT 通道（OTA YMODEM 也走这里） |
| PA11/PA12 | HUB_R2/G2 | 下半屏 R/G |
| PA15 | HUB_CLK | 移位时钟（JTAG 释放后复用） |
| PB0 | GPS_LED | 搜星指示（osTimer 100ms 翻转） |
| PB1 | GPS_ENABLE | GPS 模块电源（校时完成即断电） |
| PB3 | HUB_LAT | 行锁存 |
| PB4 | TIM3_CH1→HUB_OE | **亮度用 TIM3_CH1 PWM 驱动 OE**（Period=100，低有效） |
| PB5 | WIFI_POWER | ESP8266 CH_PD 电源（校时完成即断电） |
| PB6/PB7 | I2C1 | HTU21D（实际走软 I2C，硬件 I2C 为备胎） |
| PB10/PB11 | I2C2 | SSD1306 OLED（硬件 I2C Fast 模式） |
| PB12 | WORK_LED | 工作指示 |
| PB13/14/15 | HUB_A/B/C | 行地址 bit0~2 |
| PC14/PC15 | LSE 32.768k | RTC 时基 |
| PD0/PD1 | HSE 8M | 主时钟 |

TIM：TIM1=HAL 时基；TIM2=1MHz HUB75D 扫描（TRGO→DMA）；TIM3=OE 亮度 PWM；TIM4=**死初始化**（配了 PSC/Period/NVIC 但全工程无 Start 无消费者，坑 90）。

## 3. HUB75D 驱动双实现（本工程精华）

### 3.1 软件位拆版（hub75d.c，已废弃但仍在编译树）

16 扫描线×64 列软件移位+LAT 锁存；四套字库（16×8 时间大数字含中文月日周廿卅 / 8×8 日期 / 8×8 农历中文 / 5×7 温湿度含 ℃%）；**位拼贴排版**——跨字节 `<<4 / >>1 / <<7` 把多字段塞进 32 行像素，`DispTime/DispDate/DispLunarCalendar/DispTemperatureHumidity` 以行号为 index OR 累加进 `rgb[3][16][16]` 位图。排版数学可复用；但引脚宏有错（坑 89），**参考排版别参考引脚**。

### 3.2 DMA-ODR 现役版（hub75d_dma_odr.c，值得整段抄）

- **TIM2 1MHz（ARR=0，每 tick 触发）TRGO → DMA1_CH1 循环模式写 `GPIOA->ODR` 半字**；F103 的 TIM2 只能触发一条 DMA 通道，**GPIOB 的 LAT/ABC/OE 在 TIM2 update ISR 里直接写**——PA 走 DMA、PB 走 ISR 的混合方案是 F1 硬件限制的标准应对。
- **单行缓冲省 RAM**：`g_paRowBuffer/g_pbRowBuffer[131]`（131 ticks = 64 像素×2 CLK + LAT 高/低 + 行地址 setup），**没有全帧 ODR 缓冲**；行尾 TIM2 ISR 里 `GenerateRowFromRgb` 从 RGB 位图即时生成下一行——DMA 循环流 + ISR 换页，RAM 开销 ≈0.5KB。
- 1/16 扫描：行对 `row / row+16`（上下半屏同扫），`HUB75D_ROWS=32 SCAN_ROWS=16`。
- 帧更新 `HUB75D_DmaOdrUpdateFrame` 只重生成当前行并置 `g_frameUpdated`，剩余行由 ISR 自动跟随 `g_rgbScan`。
- 亮度 = TIM3_CH1 PWM 驱动 OE 引脚（占空比调光，非"全开全关"）。
- 电源管理：`HUB75D_DISPLAY_POWER_OFF` + 停扫描（息屏断电，PA1）。

## 4. 任务架构：模块即微服务 + 中央事件聚合

- CubeMX 只建 `displayTask` 占位（`__weak DISP_Task`），**真任务由各模块 `XXX_TaskInit()` 自建**（DISP/Scan/OLED/TH/WIFI/GPS 六模块）——模块自治、Main 不膨胀。
- `DISP_Task`（Normal7）是**中央事件聚合器**：`osEventFlagsWait` 五事件（秒级重建帧 GET_SCAN_RGB / GET_TH_DATA / GET_TIME_DATA / DISP_ON / PIR_INT）。
- **息屏 = 五任务全家 Suspend**（WIFI/GPS/TH/Scan/OLED）+ 点阵断电——任务级省电（未用 STOP，对照 smartwatch 的 STOP 方案：显示设备断电才是大头，深度睡眠收益次之）。PIR 事件 → Resume 全家 + 重置 5 分钟计时。
- 模块间通信统一模式：**事件标志（唤醒/通知）+ 消息队列深度 1（最新值语义）+ 模块自封装 `SetEvent/GetXxx` 接口**。
- 挂起两种策略并存：TH 用 `osTimerStop`（饿死事件源），GPS/OLED 用 `osThreadSuspend`（直接挂线程）——后者 Resume 后要补发 POWER_ON 事件重建状态。

## 5. WiFi/GPS 双源校时链

### 5.1 ESP8266 AT 表驱动（esp8266_at.c）

- **7 步 init 命令表 + 5 行关键字→handler 表**（WIFI DISCONNECT/CONNECTED/GOT IP/+USERTIME:/FAIL）——AT 交互全表驱动，新增命令只加表行。
- **`AT+USERTIME?`（安信可定制固件）**：ESP 内部 SNTP 对时后直接回 `+USERTIME:<ts>,"YYYY-MM-DD HH:MM:SS",<wday>`，MCU sscanf 一次拿本地时间——SNTP 报文/时区/DST 全部卸载给 ESP 固件，比 MCU 自己发 NTP 报文省一个数量级的代码。
- 校时成功 → **ESP 断电**（CH_PD 拉低）+ 停定时器；2 分钟兜底关机（连不上防耗电）；重试 5 次放弃。配网靠 ESP 常驻 Web 配网固件（不在仓库，§1）。
- DMA 接收 + 200B `pvPortMalloc` 短生命周期缓冲（用完即 free）。

### 5.2 GPS 链（gps_task.c + minmea）

- `minmea_parse_rmc` 解析 RMC → `minmea_gettime` 得 UTC → 时区换算 → `CalculateWeek` → `SetClock` 写 RTC → **GPS 断电 + 停 LED**。搜星中 osTimer 100ms 闪 LED。
- `GPS_GET_TIME` 宏开关：整模块条件编译，未启用时提供空壳 stub（`GPS_TaskInit` 返回 osOK 等）——**模块可选化编译开关模式**（与 UNIX_TIME_STAMP 备胎同族）。
- 小瑕疵：`pvPortMalloc` 失败分支里 `vPortFree(buffer)`（free NULL 无害但冗余）。

## 6. OTA 双分区 + 防御式 Boot（本工程第二精华）

### 6.1 分区与参数页

| 区 | 地址 | 大小 | 说明 |
|---|---|---|---|
| Boot | 0x08000000 | 7KB | 含 YMODEM 接收器+CRC32 |
| **参数页** | **0x08001C00** | 1KB | **放 boot 区末页**——App 升级永远擦不到（对照坑 67：参数页放 App 尾部有被链接器侵占风险，放 boot 区更安全） |
| App | 0x08002000 | 54.5KB | VTOR 由 App 侧设置 |

`app_para_t{magic=0x41505031 "APP1", fw_size, fw_crc32}`，CRC32 用标准 zlib 算法（**PC 端 Python `zlib.crc32` 直接算，无需工具链**）。

### 6.2 两阶段 magic 激活（掉电安全升级，值得背下来）

1. App 运行中收到 ESP 下发 `AT+USEROTA=<size>,<crc>`（Web 页触发）→ App **擦参数页 + 写 fw_size/fw_crc32，但故意不写 magic** → `NVIC_SystemReset()`。
2. Boot 上电：`app_valid()`（magic + size 范围 + CRC 实算三级校验）失败（magic 无效）→ 进 YMODEM 等待模式。
3. Boot 收完固件 → 实算 CRC 与参数页预期比对 → **一致才由 Boot 写回 magic** 激活 → 跳转 App。
4. 任意点掉电：magic 始终无效 → Boot 永远停留升级模式可重升。**升级请求（预期值）与激活（magic）分离在不同时间、不同执行体**——对照 OV-Watch IAP 的 FLAG 方案与加热台坑 57 的先擦后写窗口，这个设计同时规避了两者的问题。
5. 空片路径：无预期 CRC（0/0xFFFFFFFF）时收完即激活（首次烧录）。

### 6.3 jump_to_app 八步防御（比常见五件套多三防）

①SP 范围校验（0x20000000~0x20005000，非法直接 return 留在 boot）；②`__disable_irq`；③SysTick CTRL/LOAD/VAL 清零；④NVIC ICER[0]/ICPR[0] 全清 + DSB/ISB；⑤`SCB->VTOR=app_addr`；⑥**`__enable_irq`**（注释明说：CubeMX 生成的 App 不会自己开中断——漏这步 App 的中断全哑）；⑦MSR MSP + DSB/ISB；⑧BX pc 内联汇编跳转。
另配 `BOOT_DIAG` 开关：跳转前打印 sp/pc + App 区 16 字节 hexdump + 每步校验结果——**bootloader 诊断日志在量产排障时救命**，模板保留此模式。
YMODEM 为回调式：`ymodem_handlers_t{header_cb 按大小擦页, data_cb 写 flash}` 注入——与 MultiButton/portable-lib 的回调注入同族。

## 7. 日志系统三通道（trace.c）

`DBG_MODULE` 一键切换：`TRACE_NONE`（全 stub 零开销）/ `TRACE_UART`（**200B×10 条环形队列异步日志**：vsnprintf 入队 + 发送完成回调驱动出队；队列满**丢新保旧**）/ `TRACE_RTT`（fputc 重定向 SEGGER RTT，零 UART 占用）。`LOGI/LOGE` 带 LOG_TAG 模块名。加 `PreSleepProcessing/PostSleepProcessing` 空 hooks。生成带日志的工程时，这个三通道骨架可直接抄。

## 8. 时间与农历（time_run + lunar_calendar）

- `ClockRun`：秒→分→时→日→月→年进位链，闰年 `GetMaxDay`，`goto calc_week` 统一出口；`CalculateWeek` 蔡勒变体（`+140 取模防负`）。
- `GetClock/SetClock` 桥接 HAL RTC（Year+2000 还原）——RTC 是唯一真值源，WiFi/GPS 只是两个校时源。
- **农历查表法**：1901–2099 共 199 项 `uint32` 位压缩表——`0xF00000>>20`=闰月月号（0xF 无闰月）、`0x80000>>(index-1)`=12 个月大小（1=30 天）、`0x001F`=春节日期、`0x0060>>5`=春节月份；`GetLunarCalendar` 春节前后两分支+闰月 flag 交替，返回值=是否闰月。公历→农历无需任何算法库，查表 30 行搞定，直接复用。
- `time_stamp.c`：`UNIX_TIME_STAMP` 宏开关的 localtime/mktime+8h 偏移备胎（未启用）。

## 9. 传感器与显示辅链

- **HTU21D 软 I2C（RTOS 变体）**：`I2C_DelayUs(2)` 用 **`osDelay(2)` 实现**——1ms 调度粒度把 SCL 压到 ~kHz 级超慢速，但**不占 CPU、不需要 DWT**，10s 读一次的温湿度场景优雅够用。对照 OV-Watch CST816 的 NOP 循环延时（裸机高精度）——**软 I2C 延时三选型：NOP 忙等（µs 精度占 CPU）/ DWT 计数（µs 精度不占）/ osDelay（ms 粒度让出 CPU，只配 kHz 级慢总线）**。HTU21D 数据带 CRC-8 poly 0x31（Sensirion 同款，对照 SGP30）；Init 读 USER_REG 置 DISABLE_OTP_RELOAD 位。
- SSD1306 OLED：I2C2 硬件 + 1KB 显存缓冲 + `SSD1306_UpdateScreen` 按页刷；Suspend 时 `SSD1306_Off`。
- PIR：PA0 双沿 EXTI → 事件 → DISP_Task 处理（ISR 只一行 SetEvent，纪律良好）。

## 10. bug 清单（人工读码发现）

1. **坑 89（升级为坑）**：软件版 hub75d.c 的 B2 引脚宏写成 `PBout(4)`——与 OE 同一脚冲突；DMA 版头文件已修正 B2=PA4，但废弃文件仍在编译树，后来者按旧文件接屏必错。
2. **坑 90（升级为坑）**：TIM4 死初始化（MX_TIM4_Init + NVIC 5,0 全配好，全工程无 Start、无 ISR 消费者）。
3. **坑 91（升级为坑）**：`g_rgbScan`（display_scan_task.c:29 定义）非 volatile，却被 hub75d_dma_odr.c 的 TIM2 ISR 经 extern 读取、DISP_ScanTask 线程写入——validator 单文件分析零报告的跨文件盲区；768B 结构体撕裂帧症状轻微（偶发一行色错一帧）所以长期存活。
4. GPS_Task `pvPortMalloc` 失败分支 `vPortFree(NULL)`（无害冗余）。
5. TRACE_UART 队列满丢**新**消息（日志断流在最后关头——丢新保旧是取舍，量产排障时最后几条往往最值钱，可改环形覆盖最旧）。
6. readme 声称的 auto_build 三工具链构建与 ESP Web 配网固件均为空目录（文档与仓库不符）。
7. hub75d.c 同文件 `LCD_Fill` 与 `Color_Fill` 两套边界约定（xend-1 vs xend）的江协遗传在位拼贴函数里再次出现（对照坑 46 家族）。

## 11. validator 记录

29 用户文件（user__ 排除 SEGGER/script + boot 用户文件）`--density md`：**0 VIOLATION 0 WARNING**——连续第二个零违规工程（前一个 BMS firmware）。ISR 纪律真实良好：所有 HAL 回调集中在 common.c 按 Instance 分发、ISR 内只 `osEventFlagsSet`。
盲区如实记录：坑 91 的跨文件非 volatile 共享是单文件分析原理性看不见的（extern 声明无 volatile，定义在别处）——聚合模式（EMBED MANIFEST）也只覆盖引脚/向量，volatile 语义仍需人工。

## 12. 生成检查单（点阵/双源校时/OTA 类工程）

1. HUB75D 类高刷新显示：优先 DMA-ODR 方案（TIM TRGO→DMA 循环写 ODR + ISR 换页单行缓冲），软件位拆只作参考实现；F1 记得 TIM2 单通道触发限制→双端口混合。
2. 息屏策略先断外设电源（屏/模块），再考虑任务挂起，最后才是 STOP——收益顺序即如此。
3. ESP8266 校时：优先查固件是否支持 `AT+USERTIME?`（SNTP 卸载）；通用 NTP 方案只在定制固件不可得时使用。
4. OTA 设计三件：参数页放 boot 区（不是 App 尾）；两阶段 magic 激活；jump 八步（尤其 `__enable_irq`）。
5. RTOS 软 I2C：低频传感器用 osDelay 变体即可；高速器件必须 NOP/DWT。
6. 农历/节假日类查表需求：位压缩年表 + 春节锚点两分支，30 行内解决。
7. ISR 消费的数据无论定义在哪个文件，**声明处**必须 volatile（单文件检查器看不见跨文件共享）。
