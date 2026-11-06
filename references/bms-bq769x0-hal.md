# BMS 电池管理系统（BQ769x0 AFE）— HAL + FreeRTOS 架构样本

> 来源：PackLink-BMS 磐链电池包安全监测与边缘诊断系统（stm32-freertos-bms-packlink-master，gitee li-lianzhe）。
> STM32F103C8T6 + BQ76940 9 串 + FreeRTOS CMSIS_V2 + HAL + Keil；实物联调完成（docs/ 含 25 篇编号文档、硬件验收报告、16 张实测照片，配套 Qt 上位机与微信小程序）。
> 技能库**首个 BMS 样本**、首个 AFE（模拟前端）驱动样本、首个带 36 用例单元测试的工程。读码基准 2026-11，v1.0 已封版。

## 1. 工程档案与五层架构

```
firmware/
  app/        bms_app.c（初始化+双模式入口） bms_tasks.c（5 任务）
  bms/        16 个纯业务模块（monitor/analysis/protect/energy/comm/params/calibration/fault_log/...）
  drivers/    bq769x0.c（AFE 驱动） soft_i2c can_transport rs485_transport
  platform/   bms_platform（任务/延时/临界区抽象——smoke 构建空实现）
  include/    app_config.h
  tests/      bms_core_tests.c（2065 行 36 用例） smoke_main.c
board/stm32f103_bq76940_9s/    板级 BSP（8 个 board_*.c + pin_map.md + bringup 清单）
ports/stm32f103_bq76940_9s_keil/  CubeMX 生成层（Core/Src main/can/usart/gpio/it/timebase）
```

- **设计原则**（docs/01）：BMS 核心不依赖 HAL；阈值全部集中 `bms_config.h`；通信只读快照不阻塞控制；保护优先于能量；均衡仅 STANDBY/CHARGE 且相邻禁开。
- **同一套业务裸机/RTOS 双跑**：`bms_app_step(elapsed_ms)` 纯轮询入口（smoke 测试/无 OS 场景）与 `bms_app_start_tasks()` RTOS 模式共用全部业务代码——可测试性的根。
- 任务表（osPriority 相对值，栈全 256 words）：monitor=protect=High（5）> analysis（4）> energy（3）> comm（2）；defaultTask 启动壳转 500ms LED 心跳。

## 2. 依赖注入三通道（本工程最值得抄的骨架）

| 通道 | 机制 | 用在哪 |
|---|---|---|
| 运行时注入 | `bms_app_init_with_board(board_bms_app_bind)` 函数指针，板层在 bind 里 wake→绑 I2C/CAN/UART | 换板只写一个 bind 函数 |
| ops 虚表 | `soft_i2c_ops_t / can_transport_ops_t / rs485_transport_ops_t` 结构体函数指针 + user 上下文 | 传输层零 HAL 依赖；测试注入 fake 后端 |
| 链接期弱符号 | `__weak bms_params_storage_load/save`（默认 load=ERROR 用默认值、save=OK 假装成功），板级 strong 实现（Flash）接管 | 业务层无 Flash 依赖，smoke 可跑 |

- 板级 bind 全景（board_bringup_stm32_hal.c）：`board_bq76940_wakeup()`（PA8 高 100ms + settle 100ms，BQ76940 datasheet wake 时序）→ 软 I2C 绑 PB8/PB9（或 `BMS_BOARD_USE_HAL_I2C` 编译切换硬 I2C）→ CAN 绑 hcan → RS485 绑 huart1（无 DE 脚 de_port=0）→ 蓝牙绑 huart2。
- CubeMX defaultTask 只做一件事：调 `board_bms_freertos_start()`（init_with_board + start_tasks）→ 应用入口经注入进生成层，USER CODE 区几乎为空。

## 3. BQ769x0 AFE 驱动精要（bq769x0.c，614 行）

- **9S 寄存器索引表** `k_bq76940_9s_register_index = {0,1,4,5,6,9,10,11,14}`：逻辑电芯号→物理 VC 通道（9 串接法跳过 VC3/VC8~9/VC13~15，pin_map.md 有同表并注明"后续 AI 修改采样、均衡或测试时必须保持一致"）。工具帧槽位同表 `k_bq76940_9s_tool_slots`。
- ADC 校准（每次 read_sample/read_raw_diagnostics 前重读，抗热漂移）：`GAIN=365+((GAIN1&0x0C)<<1)+((GAIN2&0xE0)>>5)` µV/LSB；OFFSET=int8 mV。
- 换算：单体 `raw*gain/1000+offset`；总压 `4*gain*raw/1000+cell_count*offset`（BAT 引脚 4:1 分压）；电流 `raw*8.44µV/Rsense`（Rsense=4mΩ）；NTC `raw*382µV`（`BMS_BQ_TS_ADC_UV`，bms_config.h 集中）→ 10k 上拉分压反解 → Beta 3380 方程。
- OV/UV trip 逆换算（写 PROTECT 寄存器）：`((mV-offset)*1000/gain-base)>>4`，base=OV 0x2008 / UV 0x1000。
- SYS_STAT 故障位→统一 fault_flags：OCD=0x01→DISCHARGE_OC、SCD=0x02→SHORT_CIRCUIT、OV=0x04、UV=0x08、OVRD|DEVICE=0x30→AFE_COMM；清故障=写 1（`bq769x0_clear_fault_status(dev,0xFF)`）。
- SYS_CTRL2 读改写单 bit：CHG_ON=0x01/DSG_ON=0x02/CC_EN=0x40；写后 `read_fet_state` 回读确认。
- 均衡掩码：逻辑位→CELLBAL1/2/3，`cellbal[register_index/5] |= 1<<(register_index%5)`（每寄存器 5 位）。
- 默认配置：SYS_CTRL1=0x18（**bit4 CRC_EN=1**，见 §10 坑 83）、CC_CFG=0x19（手册推荐）。
- 采样源可切：mock（默认，smoke）/寄存器（板级 `bq769x0_set_sample_source`），`bq769x0_mock_set_sample` 注入假样本。

## 4. 保护与控制链

- 软件保护（bms_protect.c）：OV/UV 带恢复回差（4.20→4.18 / 3.10→3.15，迟滞带内保持）；电流类纯阈值无回差（掉载即恢复）；OT/LT 带 −10°C 回差。温度上限运行时可参数化（20~100°C 校验，默认 70）。
- 故障分侧关断：充电类（OV/CHG_OC/CHG_OT/CHG_LT）→charge_enabled=DISABLE；放电类（UV/DSG_OC/SCD/DSG_OT/DSG_LT）→discharge_enabled（UV 归放电侧）。
- **手动控制与保护优先级**：RS485/串口/CAN 的 MOS 开命令 = 先 `clear_latched_faults`（清 SYS_STAT 硬件锁存+软件 flags）再 set；测试专设用例 `test_serial_tool_mos_open_clears_latched_fault` 与 `test_protect_preserves_manual_mos_disable`（手动关 MOS 后 protect 不得自动重开）锁死语义。
- SOC 门限关断（bms_energy_step）：soc≥1.00 关充电、≤0.00 关放电（充满/放空自动停）。
- 均衡策略：Δ≥50mV 且最高单体≥3.30V 且 STANDBY/CHARGE 才开；**相邻禁开**（mask 查 `bit>>1|bit<<1`）；最多 2 路并发（发热限制）。

## 5. SOC：OCV 锚点 + 安时积分双路径

- `bms_analysis_step`：STANDBY 或剩余容量≤0 时用 OCV 七点表（3.10→0.00 … 4.20→1.00，线性插值）按**最低单体电压**重锚；否则安时积分 `remaining += I*Δt/3600`，clamp 0~容量，soc=remaining/容量。
- 注意双表并存：内部 SOC（0~1 float，OCV 七点表+积分）与工具帧 SOC（`serial_tool_soc_from_pack_mv` 19 档 50mV 查表，3450mV 以下断崖 40→30→20→10→5→0 模拟锂电放电平台）**不是同一数值**——上位机显示与内部 SOC 有意分层（文档 07），对比读数时须知。

## 6. 通信三协议（bms_comm.c，986 行）

- RS485 ASCII（115200，实际 TTL 无 DE 脚）：STAT/CELL/LOG/CLEAR/CANSTAT/PARAM=RESET/CHG=x/DSG=x，尾部 `\r\n ` 裁剪。
- 串口 50B 工具帧：状态帧 `AA 01`（单体按 VC 物理槽位放 offset=2+slot*2，slot=0/1/4/5/6/9/10/11/14；32 总压 mV、34 SOC%、36 |电流| mA、38 温度、39 DSG、40 CHG；**总压=Σ单体优先**，Σ=0 回退 BAT）；参数帧 `AA 88`。
- 二进制命令 `01 cmd 55`（0x12/0x13 五字节带 u16 参数）：02/03 流开闭、04/05 DSG、06/07 CHG（开=清锁存）、12/13 设参数、14 读参数、15 重置、16 BQRAW、17 清故障、18 CAN 统计。**串口与 CAN 下行（0x100）共享 `serial_tool_handle_binary_command`**，CAN 回 0x101 ACK（AA+cmd+状态码 0x00 OK/0x01 ERR/0x02 PARAM/0x03 DRIVER）。
- CAN 上报：0x01~0x05 每帧 3 串（AA+帧号+3×u16 大端，10~15 串预留填 0）、0x06 总压/SOC/电流、0x07 温度/MOS；兼容 0x180 状态帧保留。
- 防交错：参数帧后状态流停 2 拍、BQRAW/CANDIAG 后停 10 拍（holdoff 计数）。

## 7. BQRAW 诊断事务 = 无锁软互斥范本

```
g_serial_tool_diagnostic_busy=1（volatile）
→ delay 120ms（让 monitor/energy 任务走完当前拍并跳拍）
→ 保存 CELLBAL1~3 → 写 0（暂停均衡，防读数被均衡分压干扰）
→ 读 GAIN/OFFSET/SYS_STAT/CTRL2/CELLBAL+每芯原始值
→ 恢复 CELLBAL → busy=0
```
monitor/energy 每拍先查 `bms_comm_diagnostic_busy()` 再动 AFE——volatile 标志+任务跳拍实现 I2C 总线独占，无互斥量。docs/08 专篇讲 BQRAW 字段语义。

## 8. 板级移植层要点（board/bsp）

- 软 I2C（PB8/PB9 开漏+外上拉，NOP 循环延时 ~50cycles）：**写寄存器自动附 CRC-8 poly 0x07**（`crc8(addr_w|reg|value)` 第五字节，SYS_CTRL1.4 CRC_EN=1 使能侧匹配）；读未校验回程 CRC（坑 83）。
- UART 接收：单字节 `HAL_UART_Receive_IT` 循环重挂 + 64B 环形缓冲；**HAL 卡死三段防御**（board_rs485_restart_rx）：HAL_BUSY→AbortReceive 重挂；非 OK→强改 `RxState=READY` 清 PE/FE/NE/ORE 后重挂。错误回调同路径自愈。
- CAN：filter 全通过+FIFO0 中断；ISR 内 while drain FIFO 入 8 深环形（满丢最旧=新帧优先）；发送 mailbox 满 15ms 超时轮询（任务级阻塞，不进 ISR）；AutoRetransmission/AutoBusOff=ENABLE（可靠优先，对照 arm32_open 的 DISABLE 吞吐优先）。PSC=4/BS1=13/BS2=4 @36MHz=500kbps。
- **一条逻辑链两物理口**：RS485(huart1)+蓝牙(huart2) send 双发/receive 合并——Qt 上位机与 BLE 小程序同时在线共用协议。
- 参数存储：Flash 末页 0x0800FC00 单记录 10B（magic 0x5042+version+battery_id+temp_limit+XOR checksum），先擦后写（窗口极小+三重校验坏记录回退默认，风险可控；对照坑 57 磨损均衡方案）。
- HAL 时基 TIM4；HSE 8M×9=72MHz。

## 9. 单元测试与工程方法论（本仓库的"第二产品"）

- **36 用例 fail-fast 手写 runner**（无框架）：失败返回 `-行号` 定位；fake_i2c（256 寄存器内存+forced_status 故障注入）/fake_can/fake_rs485 后端经 ops 表注入——§2 依赖注入的直接红利，测试不碰硬件。
- 用例名即契约：`test_protect_preserves_manual_mos_disable`（手动关 MOS 保护不得重开）、`test_serial_tool_mos_open_clears_latched_fault`（开 MOS 必清锁存）、`test_bq76940_9s_balance_register_map`（9S 映射表锁死）、`test_bq_default_config_registers`（默认配置锁死）……
- 文档驱动开发实证：docs/00~25 编号演进（计划→架构→安全→验证清单→构建→协议→BQRAW→校准→每功能验收→封版边界→showcase）；reports/ 每次实测 CSV+MD 双格式留档；16 张验证照片编号入库；tools/ 五个脚本（Python 诊断/校准、PS1 语法构建/smoke 构建/UART bootloader 烧录）。
- 上板节奏（docs/02 八步）：先通信→后采样→后 MOS→最后均衡；MOS 命令只在低压限流工况验证。

## 10. bug/风险清单 + validator 记录

1. **I2C CRC 写带读不带**（坑 83）：SYS_CTRL1=0x18 使能 CRC 后写必须附（已做），读未校验回程 CRC——读路径丢一半保护。
2. **双 SOC 表并存**（§5）：内部 OCV+积分 vs 工具帧 19 档查表，同机两值可不同，调试时易困惑。
3. `sample_tick_ms` uint32 按 100ms 累加，49.7 天回绕→fault_log 时间戳错乱（长期运行项，记录用）。
4. 工具帧 slot 布局按物理 VC 槽位放（0/1/4/5/6/9/10/11/14），上位机侧须同表解码，换接法两端同步改（pin_map.md 已声明该约束）。
5. board_bq76940_gpio 的 PA8 初始化不自含 GPIOA 时钟（依赖 MX_GPIO_Init 先开）——真实但轻微（坑 85）。

**validator**（22+7 文件，--density md）：firmware 全 22 文件 **0V0W 全 PASS**（首个核心业务零违规工程）；bsp 2V：①board_bq76940_gpio GPIO 无本文件时钟=真实轻微；②board_comm ISR 访问 `g_can_ctx` 报非 volatile=**误报**——结构体整体非 volatile 但 rx_head/rx_tail 成员 volatile，是 SPSC 环形队列标准写法（坑 84 记录判定规则）。1W：ISR 内 drain FIFO while=事件驱动带上界，豁免。

## 11. 生成检查单（Agent 产出 BMS 类代码时）

1. 采样/均衡/显示三处共用同一张"逻辑电芯→VC 通道"映射表，单一来源（头文件常量），文档随表。
2. AFE 驱动层校准值（gain/offset）每次采样前重读；换算公式集中在驱动一处。
3. 保护阈值全部集中一个 config 头；带恢复回差的写清 protect/release 两列。
4. MOS 手动开命令必须"清锁存→开→回读确认"三步；保护任务不得覆盖手动关闭。
5. 诊断/标定类长事务与周期采样互斥：busy 标志+跳拍，或互斥量，禁止裸并发。
6. 均衡掩码生成加相邻禁开+最大并发数双限制。
7. 通信命令处理器单份实现，多通道（UART/CAN/BLE）转发复用；ACK 帧带命令码+状态码。
8. 存储记录带 magic+version+checksum，坏记录回退默认值而不是拒绝启动。
9. 每个可注入依赖（I2C/存储/传输）留 ops 表或 weak 钩子，使业务可脱离硬件测试。
