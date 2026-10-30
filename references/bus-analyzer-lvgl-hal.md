# Linno 多协议总线分析仪（F407VET6 + LVGL 8.2 + CMake/CLion）

> 源工程：`D:\stm32project\Linno\Linno`（用户提供，实测上板）。
> 形态：手持式多协议总线分析仪/调试器——9 个通信端口（3×UART 含 RS485、2×CAN、2×SPI、2×I2C）实时监视 + 任意端口间网桥转发 + 信号源/万用表探针 + USB CDC（预留）+ SD 卡配置。
> 样本价值：**技能库首个 CMake/CLion + arm-none-eabi-gcc 工程**（前 21 个样本全是 Keil/IAR），也是首个 F407 大资源 LVGL 产品级应用（GUI 自研 APP 框架 + 1.2MB 全字库）。

## 1. 工程结构与构建链

```
Linno/
├── CMakeLists.txt          # arm-none-eabi-gcc；-mcpu=cortex-m4 -mthumb -mthumb-interwork
│                           # -ffunction-sections -fdata-sections + -Wl,-gc-sections
│                           # file(GLOB_RECURSE) 收源；STM32F407VETX_FLASH.ld 链接脚本
│                           # Debug=-Og -g / Release=-Ofast；POST_BUILD objcopy 出 hex/bin
├── CLion 工程文件 + .idea + cmake-build-debug-mingw + OpenOCD stm32f4discovery.cfg
├── Core/                   # CubeMX 生成（.ioc：HSE 12MHz→PLL M=6/N=168/P=2=168MHz，PLLQ=7→48M USB；FLASH_LATENCY_5；APB1=/4 APB2=/2）
├── Drivers/                # F4 HAL + CMSIS
├── FATFS/ USB_DEVICE/      # SD 卡文件系统 + CDC 模板
└── User/                   # 2.5MB 业务层
    ├── HomePage/           # 椭圆轨道 3D 轮播主菜单（Ellipse_Track_Map[1000] 预生成坐标）
    ├── MonitorPage/        # CAN/IIC/SPI/UART 四协议监视页 + MainMonitor 九端口总览
    ├── Probe/              # 信号源+万用表（DAC 电压/PWM 频率占空比/ADC 读数）
    ├── Transfer/           # 九端口总线网桥
    ├── Setting/            # 系统设置（FATFS 存档）
    └── UI/                 # 自研 GUI 框架 + FONT12 全字库 1.2MB + 5×50×50 ARGB 图标各 181KB
```

**CMake 工程四件事**（Keil 用户迁移清单）：链接脚本 `.ld` 替代分散加载；`file(GLOB_RECURSE)` 收源（新增文件要重跑 cmake——不是自动的）；POST_BUILD `objcopy -O ihex/bin` 出烧录文件；调试走 OpenOCD（`stm32f4discovery.cfg`）而非 ULINK/ST-Link Keil 插件。

## 2. 硬件资源表（.ioc 实测提取）

| 功能 | 引脚 | 备注 |
|---|---|---|
| FSMC LCD | D0~7=PD14/15/PD0/PD1/PE7~PE10，A16(RS)=PD11，NE1=PD7，NOE=PD4，NWE=PD5 | 16 位总线，A16 当 RS（正点原子 LCD 风格） |
| 触摸中断 | PE0=EXTI0 | |
| USART1 | PA9/PA10 + DMA2_S2/S7 | |
| USART2 | PA2/PA3 + DMA1_S5/S6 | |
| USART3=RS485 | PD8/PD9 + DMA1_S1/S3 | **UI 命名"RS485"，句柄 huart3** |
| CAN1 | PB8/PB9 | RX0 中断接收 |
| CAN2 | PB12/PB13 | |
| SPI1 | PA5/PA6/PA7 | UI 命名"SPI 2"（错位见 §8） |
| SPI3 | PB3/PB4/PB5 + NSS PA15 | UI 命名"SPI 1"=hspi3 |
| I2C1 | PB6/PB7 | EV+ER 中断，从机模式监听 |
| I2C2 | PB10/PB11 | |
| SDIO | PC8~PC12+PD2 + DMA2 S3_TX/S6_RX | FATFS |
| USB OTG FS | PA11/PA12 | CDC 模板未接业务 |
| ADC1 | CH8=PB0 + CH15=PC5，扫描 2 通道，DMA2_S4 CIRCULAR word | 3 周期采样，DMAContinuousRequests |
| DAC | PA4（SharedAnalog） | DAC_OUT1 探针输出；PA4 同时 ADC 可回采 |
| PWM 探针 | TIM1_CH3=PE13、TIM2_CH2=PA1 | Probe 页动态改频率/占空比 |
| 杂项输出 | PB1/PB15/PC4/PD10/PE1/PE11 | 背光/控制 |
| 时钟 | HSE 12MHz PLL×168；LSE 未用 | 168MHz，USB 48M 走 PLLQ |

## 3. 架构：全中断+DMA 数据流，update 标志消费者模式

```
ISR/DMA 回调（HAL 弱回调=中断上下文）
   │  只做两件事：收数据进缓冲、置 XXX_Rx_update 标志（+Textarea_Add_Len 计数）
   ▼
LVGL lv_timer（Monitor_Cycle，页面持有）
   │  轮询消费 update 标志 → itoa_hex_8 → textarea 追加显示（每端口最多 8 字节预览）
   ▼
页面销毁：LV_EVENT_DELETE 回调里 lv_timer_del（泄漏纪律）
```

- **双时基**：SysTick=HAL_IncTick（HAL 超时用）+ **TIM7 1ms 中断=lv_tick_inc(1)**（LVGL 心跳独立）。TIM7 中断优先级高于 SysTick 之外的阻塞源，动画不被 HAL_Delay 类操作饿死。
- HAL 弱回调全景：`HAL_CAN_RxFifo1MsgPendingCallback`（CAN 收）、`HAL_UARTEx_RxEventCallback`（三串口收，DMA+IDLE 接收至事件）、`HAL_I2C_AddrCallback`/`SlaveRxCpltCallback`（I2C 从机地址匹配/收完）、`HAL_SPI_TxRxCpltCallback`/`ErrorCallback`（SPI 收发完）。**这些回调都在 IRQ 上下文执行——纪律同 ISR**（本工程 30 处非 volatile 访问是反面教材，见 §9 validator）。
- `General_Buffer[2048]` 全局共享缓冲（多页面复用，省 RAM 的代价=非可重入，页面切换期共用无并发冲突因为单页面活跃）。

## 4. 自研 GUI 框架（APP 模型）

- `APP` = 函数指针类型（每 App 一个建屏函数）；`GUI_APP_Enter/Switch/Exit` 封装 `lv_scr_load_anim(MOVE_LEFT/RIGHT, 500, 200, auto_del)`。
- **右滑=退出手势**：`lv_indev_wait_release` 防止退出后残留按压事件。
- **软键盘**：textarea 聚焦自动弹（READY/CANCEL/DEFOCUSED 三态销毁）；高度避让=键盘上方留白计算。
- 状态栏：顶部 5 秒 `lv_timer` 自动删；电池 5 档 LV_SYMBOL 图标+百分比（ADC `adc_val[0]` 线性映射 BATTERY_FULL=2500/ZERO=2200）。
- 主题/多语言：`themeColor` 全局 + Language 表。
- **椭圆轨道 3D 轮播主菜单**（Home_UI.c）：`Ellipse_Track_Map[1000]` 预生成椭圆坐标；4 图标均布（index/±250/±500/±750）；`lv_timer` 1ms 里跑 **PD 控制器惯性滚动**（Kp=0.2 Kd=0.01，输出限幅=ui_speed_max 低速 20/高速 1000，err==0→动画完成回调）；`lv_img_set_zoom=(y-30)*5*zoom_base` 近大远小；y 最大者 `lv_obj_move_foreground`；RELEASED 吸附最近 250 格；顶部下拉呼出状态栏；开机低速转一圈入场。**用 PD 控制器做 UI 缓动**是 LVGL 动画的非依赖方案（不用 lv_anim 也能做惯性）。

## 5. Monitor 监视引擎

- 每协议一页（CAN/IIC/SPI/UART_UI.c），`selected_huart/hcan/hspi/hiic` 句柄选择器决定当前监听对象，MainMonitor 总览页选端口进对应 App。
- `Monitor_Cycle` lv_timer 消费 `XXX_Rx_update` 标志；每端口显示最近 ≤8 字节 hex（itoa_hex_8）+累计长度（Textarea_Add_Len）。
- 页面 `LV_EVENT_DELETE` 里 `lv_timer_del`——**LVGL 页面持有系统资源的标准清理位**（漏删=定时器空转+野指针访问已删控件）。

## 6. Transfer 九端口网桥

- `port_code`：uart1/uart2/rs485/can1/can2/spi1/spi2/iic1/iic2；`PORT{id_address, transfer_port_en[9]}` 使能矩阵。
- `Transfer_Transmit(self, data, len)`：任一端口收到 → 广播到所有其他使能端口（`HAL_UART_Transmit_DMA` / CAN 标准帧 / `HAL_SPI_Transmit_DMA`）。
- `if(0&&len>=2)` 死代码开关：前 2 字节当目标地址的定向转发被禁用（`0&&` 短路——改 `1&&` 即启用）。功能开关用死代码而非配置项，是产品代码里的隐患写法。

## 7. Probe 信号源 + Setting

- **Probe**：DAC 电压输出（HAL_DAC_SetValue 12B 右对齐）+ PWM 频率/占空比（TIM1_CH3，`set_pwm_param` 运行时改 PSC/ARR）+ ADC 实时读数 lv_timer；三通道交互=textarea 直输 + 点击± + **长按连调**（LONG_PRESSED→PRESSING 连续加减，RELEASED 停）——数值调节控件的三态连调范式。
- **Setting**：配置存 SD 卡 `0:setting.txt`（FATFS 二进制裸写整个结构体：背光/方向/息屏时间/存储路径/主题）；`Setting_Load` 开机读回。`Setting_Check_Sleep`=超时关背光（**假休眠**——LCD 背光关，不进 STOP；触摸 EXTI0 唤醒恢复）。四协议配置（波特率/位宽等）也从 SD 卡 `Config_Load` 后 `HAL_XXX_Init` 重配——**这就是业务文件重配外设、时钟在生成层的场景**（validator 规则 6 的 cube_ctx 降级判据来源）。

## 8. bug/隐患清单

1. **硬浮点未开**：CMakeLists 里 `-mfloat-abi=hard -mfpu=fpv4-sp-d16` 被注释——F407 的 FPU 空转，全部浮点走软件库（椭圆轨道 PD 控制器每毫秒浮点运算、电池线性映射都在软浮点上跑）。UI 仍流畅说明余量大，但 Flash/性能白亏。→ 坑 70。
2. **UI 端口命名与句柄错位**：按钮"SPI 1"→hspi3、"SPI 2"→hspi1；"RS485"→huart3。功能正确但读码/排障时心智负担大（用户面板命名≠原理图命名≠句柄命名三套体系）。
3. **USB CDC 空挂**：`CDC_Receive_FS` 只重挂缓冲，数据丢弃——USB 功能预留（模板未接业务，诚实说是半成品）。
4. **`if(0&&...)` 死代码开关**：定向转发地址功能用短路禁用，编译器优化掉但源码层面语义混乱。
5. **30 处 HAL 回调写非 volatile 全局**：`XXX_Rx_update`/`receive_data`/`Textarea_Add_Len` 等被 IRQ 写、lv_timer 读，无一 volatile——M4 对齐 32 位单访问原子所以能用，多变量组（数据+长度）无一致性快照。→ 坑 71。
6. DMA2_Stream1=memtomem 空挂（CubeMX 声明未用）。
7. `hs_err_pid*.log` JVM 崩溃残留入库（CLion 崩溃日志，应 gitignore）。

## 9. validator 记录（--density f4）

34 VIOLATION 首跑 → 修复 4 处误报后 30V3W：
- **真实 30 条**：CAN_UI 8（RxFifo1 回调写 8 个非 volatile）、IIC_UI 8（Addr/SlaveRxCplt 回调）、SPI_UI 8（Error/TxRxCplt 回调）、UART_UI 6（RxEventCallback）。
- **误报 1 修复**：`SDIO_IRQHandler` 未知向量——F4_VECTORS 漏收该向量（F4 系 SDIO 中断真名，F7 起才叫 SDMMC），已补。
- **误报 3 降级**：IIC/SPI/UART_UI.c「初始化无时钟」——业务文件 `HAL_XXX_Init(&h句柄)` 重配，时钟在 Core/Src 生成文件。规则 6 新增 `cube_ctx` 判据（MX_ 调用/include 生成层头/`HAL_..._Init(&hxxx)` 句柄风格三选一→时钟类降 WARNING 人工核对）；SPL 轨与裸指针初始化（dw1000.c 实测保持 1V）不变。
- 回归：bad_sample 10V1W FAIL 不变、templates/main.c 与 main_hal.c PASS 不变、dw1000.c 1V FAIL 不变。
- 统计脚本教训：`(\d)` 只抓一位数（"10"截成"1"）——批量统计永远 `(\d+)`。

## 10. 生成检查单（本工程提炼）

1. CMake 工程交付必查 `-mfloat-abi=hard -mfpu=fpv4-sp-d16`（M4/M7）；RTOS 还要 `FPU 上下文栈`（FreeRTOSConfig 的 configUSE_TASK_FPU_SUPPORT 或 port.c FPU 断言）。
2. UI/面板命名 ↔ 句柄映射表写进文档（防止三套命名体系错位）。
3. HAL 弱回调按 ISR 对待：volatile、禁阻塞、禁刷屏。
4. 页面持有的 lv_timer/动画必须在 LV_EVENT_DELETE 释放。
5. LVGL 双时基方案：TIM 独立供 lv_tick，SysTick 归 HAL。
6. 功能开关走配置（SD 卡/EEPROM），不走 `if(0)` 死代码。
7. 大字库/图标资源交付前看 **map 文件**的 .rodata 实占：C 数组源文件体积（本工程 User 层 2.5MB、FONT12 源文件 1.2MB）≠ 链接后二进制（点阵数据约源文件 1/5~1/6）；总量必须 < 芯片 Flash（VET6=512KB）。
