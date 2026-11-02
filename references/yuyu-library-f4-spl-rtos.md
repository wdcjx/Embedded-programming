# YUYU 自建外设库（F407+SPL+FreeRTOS：技能库唯一零 VIOLATION 正面教材）

> 源工程：`D:\stm32project\stm32-opensource-library-STM32F407ZGT6\`（STM32F407ZG + **SPL 标准库** + FreeRTOS，Keil/RTE）。
> 状态：**文件级读码**，未编译未上板。价值：技能库 F4 轨首个非 HAL 样本 + 自建分层驱动库范本 + **17 文件 validator 0 VIOLATION（至今唯一）**——全库 MISRA 风格，正面教材基准。

## 1. 工程档案

- 芯片：STM32F407ZG（uvprojx Define=`USE_STDPERIPH_DRIVER,STM32F40_41xxx,HSE_VALUE=8000000`）——F4 用 SPL 而非 HAL 的实证（F4 SPL 是 ST 最后一代 SPL，F7 起无）。
- 四目录：`1.STM32F407ZGT6`（启动/CMSIS）、`2.FreeRTOS_Source`（官方全源码树+portable 全家）、`3.FreeRTOS`（三本中文 PDF 教程）、`4.Project_Source`（用户区：User + YUYU_Library + Libraries(SPL) + FreeRTOS 副本）。
- `User/headfile.h` 四段式总include：system → FreeRTOS → driver → device——架构自文档化。
- FreeRTOSConfig 要点：`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`（抢占 5~15 才可调 FromISR API）、`configCHECK_FOR_STACK_OVERFLOW=2` + Malloc hook 开、`xPortPendSVHandler/xPortSysTickHandler` 宏接管，it.c 剥到只剩 fault 类（SVC 留给 port 汇编强符号）。
- 作者在 `.skill/` 自带 embedded-dev.md（27KB 分层评级制技能文档，LV1~LV3 按芯片能力分档适用规则）与 embedded-doxygen.md——**工程作者自己写 Agent 技能**的实例，其"芯片能力分层"维度与本技能库"库生态分层"互补。

## 2. 分层架构（SPL 版三层思想）

```
User/          main.c  isr.c（ISR 集中，只做 uart_rx_isr_push）
YUYU_Library/
  device/      button  ds18b20  screen(三屏适配)  wifi_esp32_c6   ← 业务封装层
  driver/      gpio soft_i2c soft_spi one_wire uart ssd1306 st7789 st7796
               system_delay screen_font(67KB 字模)               ← 通用驱动层
Libraries/     SPL 标准库
```
与 hal-driver-patterns.md 的 HAL 三层结构同思想不同底座：driver 只依赖 gpio/system_delay 等更底层 driver，device 组合 driver 成设备语义——层间单向依赖。

## 3. main.c：RTOS 应用骨架正面教材

- 三任务全 `vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(PERIOD))` **绝对周期**（对比 vTaskDelay 相对延迟的漂移）；sensor_task 是预留空转模板。
- `configASSERT(xTaskCreate(...) == pdPASS)` 逐个断言创建结果；`vApplicationMallocFailedHook/vApplicationStackOverflowHook` 双钩子实现（关中断死循环）。
- **RECEIVE_TASK_MODE 三态编译期切换**（NULL/UART/WIFI）：一套 main 三形态，`#if` 全部成对干净。
- 跨任务数据：`wifi_display_data + volatile wifi_display_pending` + `taskENTER_CRITICAL` 复制快照——任务级握手无队列时的正确替代。
- 瑕疵：WiFi 明文密码硬编码在 wifi_config（开源演示可接受，产品须 NV/配网）；control_task 用 count 分频翻转 LED（20ms×10）非阻塞 ✓。

## 4. wifi_esp32_c6：SPI 二进制协议 WiFi（非 AT 指令！）

技能库物联网连接第四范式（GSM AT / NRF24L01 寄存器 / 蓝牙透传 / **SPI 协议网关**）：ESP32-C6 跑定制固件当 SPI 网关，STM32 用 4 线 SPI+INT 就绪线对话。

- 协议帧：4 字节头 `{command, reserve, length_be16}`（struct memcpy 直传字节流——reserve 恰好补齐，M4 小端+无填充时成立）。命令 0x02=透传 DATA/0x10=SET_WIFI(payload="ssid\r\npwd\r\n")/0x11=SET_SOCKET(type/host/port\r\n)/0x20=GET_VERSION/0x22=GET_IP/0x03=UDP 立即发送/0x04=CLOSE；应答 0x80=OK/0x90,0x91=DATA_START/END/0xA0=VERSION/0xA2=IP。
- **握手**：每次交易前后 `wait_wifi_ready(timeout)` 轮询 INT 引脚（模块忙=LOW），SPI 从机就绪线全双工协议标配。
- **固定 32 字节短包**：命令/应答永远按 WIFI_SHORT_PACKET=32B 全双工交换（tx 填 0 对齐），长数据（≤4088B chunk）头体分开传。
- 接收：环形 FIFO（rx_fifo + head/tail，满即静默丢——宽容丢弃策略）+ `wifi_esp32_c6_read` 先取 FIFO 再拉新包。
- 超时齐备：wait_wifi_ready 1000ms / connect WiFi 10s / socket 50s——对比 NRF 死等（坑 45）是正面写法。
- SPI1 硬件外设（模式 3，CPOL=High/CPHA=2Edge）+ CS/RST/INT 三根 GPIO。

## 5. uart.c：RTOS 串口驱动教科书（与排队闸机任务通知互补）

- **ISR 侧**：`uart_rx_isr_push` 写 volatile 环形缓冲 + `vTaskNotifyGiveFromISR`（计数语义）+ `portYIELD_FROM_ISR`——ISR→任务官方推荐通道完整实现；isr.c 里 USART1_IRQHandler 只判 RXNE 后一行转交。
- **NVIC 抢占 15 注释明写** `≥configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，可安全调用 FromISR API`——作者知道约束且写在代码里（多数工程的隐性坑，这里是显性正解）。
- **任务侧**：`uart_wait_for_rx` 临界区内注册句柄+查空+`ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` 阻塞（先查后等防丢通知竞态）。
- **发送**：互斥锁帧级原子（整帧头+序号+数据+CRC+尾在锁内），多任务并发发送不撕裂。
- **可配置帧协议**：USE_TRANSFER（裸 4 字节/带帧）× USE_CRC（无/CRC-8 poly 0x07/CRC-16 Modbus 0xA001）编译期组合；9 态帧状态机解析，非数据字节回退到 HEAD 候选（流式再同步，不丢帧）。
- 数据编码 4 位十进制 ASCII（0~9999），可读性优先；帧序号自增但接收端只上报不查重（接口预留）。
- 瑕疵：`uart_receive_byte` 轮询死等版与 ISR 模式并存（无人调用=死码候选）；`uart_write_byte` 忙等 TXE 持锁（时长=一字节，可接受）。

## 6. screen.c：编译期三屏适配层（无 GUI 库标准解法）

- DISPLAY_DRIVER 三选一（ST7789 240×320 / ST7796 320×480 / SSD1306 OLED），同一套 `display_*` API；`screen.h` 按 driver 映射 SCREEN_WIDTH/HEIGHT、字体尺寸、颜色宏。
- **缓冲型 vs 直写型差异抹平**：`display_refresh_begin/end` 在 TFT 分支是空函数、在 SSD1306 分支是 `display_batch++/--` 嵌套计数，归 0 才 `ssd1306_refresh()` 推屏——上层（show_string_auto）无条件包 begin/end，两种驱动形态都正确。对照 OV-Watch/Linno 用 LVGL flush_cb 抽象同一问题。
- **关键字着色文本**：`screen_keyword[]`（SUCCESS/绿、WARN/黄、ERROR/红）+ 大小写折叠匹配（表全大写，只折叠待匹配串）+ `[关键字]` 与裸关键字两形态 + 画原文保持原大小写只取颜色。
- `display_string_width`：UTF-8 宽度计算（2/3 字节序列按 CJK 16px），换行只对 ASCII 自动（CJK 不截半）。
- 维护性瑕疵：三个 `#elif` 分支各复制一份 display_string_width/fill_rect（编译期互斥的模板复制，改一处须同步三处）。

## 7. 驱动层速记

- **soft_i2c**：开漏+上拉（真 I2C 电气，对比江协推挽版）5µs≈100kHz、ACK 超时 250 次。
- **one_wire**：`one_wire_enter/exit_timing` 用 **PRIMASK 保存恢复**保护 µs 级时序段（RTOS 任务切换不打断时序的正确做法，比关调度器轻）；开漏释放读。
- **ds18b20**：分层教科书（driver=1-Wire 时序、device=DS18B20 协议）；**非阻塞三段式** start_conversion → conversion_ready（轮询 DQ，对比 HAL_Driver_Lib 忙等读 0xFF）→ read_scratchpad（**CRC8 校验** + 9~12bit 分辨率感知掩码）；错误码枚举完整（NO_DEVICE/ARGUMENT/CRC/NOT_READY）。
- **st7789**：软 SPI 位拆（NOP 可调延时）、8 方向 dir_table（横屏 MV=1 组 0x28/0xA8/0x68/0xE8 + 竖屏组，窗口随 MV 互换）、X/Y_OFFSET、四档字体（6x8 行式/8x16 列行式/16x16/24x24——**同库两种取模方向并存**，screen_font.c 各表自带）；st7796 同构 320×480。
- 性能注意：`draw_point` 逐点开窗（CASET/RASET/RAMWR 每像素一遍），show_char 逐像素画——软 SPI 场景结构清楚但全屏字符吞吐低（可接受：显示任务 100ms 周期）。
- **system_delay**：DWT CYCCNT 周期计数（F4 168M 精确 µs）——RTOS 工程 delay 正解（不动 SysTick）。

## 8. bug/瑕疵清单（无安全级，全部轻量）

1. WiFi 明文密码硬编码（开源演示；产品须配网/NV 存储）。
2. `uart_receive_byte` 轮询死等版无人调用（与 ISR 模式并存，死码候选）。
3. SPI/UART 忙等无超时（TXE/RXNE while 空转）——任务级非 ISR，validator WARNING 合理；总线硬件故障会挂死任务（对比 wait_wifi_ready 的超时齐备，风格不一致）。
4. wifi rx FIFO 满静默丢（无溢出计数器——宽容丢弃可用，诊断时无痕迹）。
5. screen.c 三分支复制（模板复制，同步维护风险）。
6. st7789 逐点开窗性能（结构优先的取舍）。

## 9. validator 记录

17 用户 .c（--density f4）：**全 PASS，0 VIOLATION——技能库至今唯一全绿工程**；4 WARNING（wifi_esp32_c6 ×2、uart ×2，全为任务级忙等 TXE/RXNE，规则正确报级）。正面教材基准：以后生成代码的纪律上限参照。

## 10. 生成检查单（从此库提炼的模板级正解）

1. 周期任务骨架：`last_wake_time=xTaskGetTickCount()` + `vTaskDelayUntil` + 任务参数 `(void)pvParameters` + static 封装 + 全字面量 U 后缀。
2. `configASSERT` 包所有 xTaskCreate；Malloc/StackOverflow 双钩子必须落地实现。
3. ISR→任务数据通道：volatile 环形缓冲 + `vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR`；任务侧临界区注册句柄 → 查空 → `ulTaskNotifyTake`。FromISR API 的 NVIC 抢占 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY，注释写明。
4. SPI 协议外设模板：命令/应答枚举 + 定长头 struct + INT 就绪线 + 全交易超时 + 固定短包对齐交换。
5. 多屏适配：display_* 统一 API + refresh_begin/end 批量语义（缓冲型计数推屏/直写型空实现）+ 编译期 driver 三选一。
6. 1-Wire/时序敏感段：PRIMASK 保存恢复式临界区（不是长关中断）。
7. DS18B20 非阻塞三段式 + scratchpad CRC8 + 分辨率掩码，禁止忙等转换。
