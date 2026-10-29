# smartwatch-lvgl-lp-hal.md — 智能手表：FreeRTOS+LVGL+STOP 低功耗+IAP 双固件（F411CEU6 HAL 实测工程）

> 基准工程：OV-Watch V2.4（No-Chicken 开源智能手表，github.com/No-Chicken/OV-Watch）。
> 用户本机镜像 D:\stm32project\OV-Watch-main\，作者实测上板运行（含 3D 打印壳与 Gerber 硬件）。
> 芯片：STM32F411CEU6（UFQFPN48，512KB Flash/128KB RAM），CubeMX 6.9.2 + FW_F4 1.27.1 + MDK V5.32。
> 本文档定位：可穿戴/低功耗/LVGL GUI/IAP 升级四类需求的生成基准。状态=读码级实测（作者上板），本文数字均出自源码。

## 1. 工程三件套与外设资源

三件套：`Software\IAP_F411`（BootLoader：HAL 裸机+Ymodem+菜单）、`Software\OV_Watch`（APP：HAL+FreeRTOS CMSIS_V2+heap_4+LVGL 8.2+19 个 SquareLine 风格页面）、`lv_sim_vscode_win`（PC SDL 模拟器，与板上 GUI_App 同源代码）。

时钟：**无外部高速晶振**——HSI/8(PLLM=8)×100(PLLN)/2(PLLP)=100MHz（F411 满血）；LSE 32.768kHz 保留给 RTC（低功耗必需）；FLASH_LATENCY_3；APB1=HCLK/2=50MHz。省 BOM 的量产做法。

| 外设 | 引脚 | 用途 |
|---|---|---|
| SPI1 | PB3=SCK、PB5=MOSI（TX-Only 单工，50MHz，模式 3） | ST7789 推屏，DMA2_Stream2 半字 |
| TIM3 CH3 | PB0 | 背光 PWM（PSC=99/ARR=300，占空比 5~100%） |
| ADC1 IN1 | PA1 | 电池电压（×2 分压） |
| USART1 | PA9/PA10 9600 | 蓝牙透传（JDY 类），RX DMA+IDLE |
| RTC | LSE | Calendar + WakeUp（32768/16=2048Hz，counter=2000≈0.98s 周期唤醒） |
| HAL 时基 | TIM1（SYS_VS_tim1） | FreeRTOS 下 HAL 时基迁 TIM，SysTick 归内核 |
| 主电源自锁 | PA3 输出 | Power_Enable 拉高保持供电；关机拉低断电 |
| 充电检测 | PA2 双沿 EXTI | 插拔充电器都产生事件/唤醒 |
| 蓝牙电源 | PA8 | KT6328 单键开关机 IC 当外设电源开关 |
| 外部看狗 | PB1=EN(低有效)、PB2=WDI | GPIO 喂狗，可 Disnable（IWDG 做不到） |
| 软 I2C① | PB4=SDA、PB6=SCL | CST816 触摸（iic_hal 对象化 iic_bus_t） |
| 软 I2C② | PB10/PB11 | MPU6050（eMPL DMP） |
| I2C 传感器 | 同②总线 | AHT21 温湿度、EM7028 心率(.lib 算法)、LSM303 罗盘、SPL06 气压 |
| EEPROM | BL24C02 | 设置+跨日步数（软 I2C） |
| KEY | KEY1(PA5 EXTI)、KEY2(PB4→EXTI4) | 短按功能/长按 3s 关机 |
| MPU INT | PB12 EXTI15_10 | motion 中断=抬腕唤醒硬件源头 |
| LCD 控制 | PB15=RES、PB14=CS、PB13=DC（推挽 50MHz） | ST7789 |

## 2. IAP 双固件升级链（Boot+APP）

Flash 布局（F411 扇区：16K×4 + 64K×1 + 128K×3）：
- Boot：sector 0~1（0x08000000~0x08007FFF，32KB）
- **APP FLAG：sector 2（0x08008000）存 ASCII `"APP FLAG"` 8 字节**——"存在合法 APP"的标志
- APP：sector 3 起（APPLICATION_ADDRESS=0x0800C000），Keil IROM1 起始同步改 0x0800C000

**掉电安全升级序**（menu.c SerialDownload）：
1. `FLASH_If_Erase_One_Sector(2)` 先擦标志区（先清"有效标志"再写数据——升级中途掉电无 FLAG→不跳转→可重升。对照坑 57 加热台"先擦数据再写"的反面）
2. `Ymodem_Receive` 收固件逐块写 APP 区（1024 字节块+CRC16，ST 官方实现）
3. 成功后写 "APP FLAG" 两字到 sector 2
4. 失败码：-1 超容量 / -2 校验失败 / -3 用户中止

**Boot→APP 跳转五件套**（menu.c/`IAP_main.c`，两处一致）：
```c
SysTick->CTRL = 0x00;  SysTick->LOAD = 0;  SysTick->VAL = 0;  // ①关 SysTick 三寄存器
__disable_irq();                                             // ②关全局中断
JumpAddress = *(__IO uint32_t*)(APPLICATION_ADDRESS + 4);     // ③向量表第 1 项=Reset_Handler
Jump_To_Application = (pFunction)JumpAddress;
__set_MSP(*(__IO uint32_t*)APPLICATION_ADDRESS);              // ④恢复主栈指针（第 0 项）
Jump_To_Application();                                       // ⑤跳转
```
APP 侧配对：`main` 第一行 `SCB->VTOR = 0x0000C000U;`（重定位向量表，注释「this must set same as keil setting」）+ SysInit 区 `__enable_irq()`。Boot 关中断/APP 开中断配对成立。
入口判定：上电 KEY1 按住 500ms→Boot 菜单（LCD 显示 Bootload+串口菜单 1 下载/3 执行）；无 APP FLAG→"No App!"。
跳转前未 DeInit 外设（LCD/USART 时钟仍开）——APP 侧 MX 重配覆盖，可行；严谨做法可在 Boot 侧复位用过的外设。

## 3. 任务架构（13 任务 + 6 队列 + 1 事件标志）

| 任务 | 优先级 | 栈(字) | 职责 |
|---|---|---|---|
| HardwareInit | High3 | 1280 | 一次性初始化，vTaskSuspendAll 包裹，跑完 vTaskDelete(NULL) 自删除 |
| WDOGFeed | High2 | 128 | 100ms 喂外部看狗（任何任务卡死→芯片级复位） |
| StopEnter | High1 | 2048 | STOP 睡眠/唤醒序列（栈需求大：SystemClock_Config 重配） |
| IdleEnter | High | 128 | 两级休眠计时（LTime 息屏/TTime 深睡） |
| Key | Normal | 128 | 按键扫描→Key_MessageQueue |
| MPUCheck | Low2 | 384 | 300ms 轮询抬腕姿态；垂手且在 Home/Menu/Set 页→发 Stop 消息（翻腕自动息屏） |
| DataSave | Low2 | 640 | EEPROM 保存（深度 2 队列） |
| LvHandler | Low | 3072 | LVGL 主循环（大栈） |
| ScrRenew/SensorData/HRData/ChargPage/MessageSend | Low1 | 640~1280 | 屏幕/传感器/心率/充电页/蓝牙消息 |

消息驱动而非共享状态：队列深度全 1（事件语义），DataSave=2。`HardIntEventHandle` 事件标志组承接 ISR 事件（UART IDLE/充电/MPU INT，ISR 里 `osEventFlagsSet`——FromISR 语义安全）。

初始化序列（HardwareInitTask）：RTC WakeUp 2000 设置→UART DMA+IDLE 使能→TIM3 PWM 启动→电源/按键→**五传感器逐个 init 重试 3 次**（ConnectionError 状态，失败不阻塞开机，UI 显示占位）→成功者立即 Sleep（罗盘/心率按需唤醒）→EEPROM 读抬腕/省电开关+**跨日步数恢复**（存日期+步数，同日开机 dmp_set_pedometer_step_count 恢复）→BLE init 后立即 Disable→LCD 开机画面→lv_init+lv_port_disp/indev+ui_init→ResumeAll→自删除。

## 4. 低功耗链（本文档核心）

**两级休眠**（IdleEnterTask）：
- 无操作 LTime 秒 → 背光降到 5%（LCD_Set_Light(5)，不熄屏）
- 无操作 TTime 秒 → 发 Stop 消息 → StopEnterTask 进 STOP
- IdleBreak（触摸/按键/LVGL 活跃）→ IdleTimerCount 清零+背光恢复 ui_LightSliderValue

**STOP 序列**（user_RunModeTasks.c StopEnterTask，RTOS 低功耗标准范式）：
```c
睡前: HAL_UART_MspDeInit(&huart1);      // GPIO 回 Analog 降漏流
      LCD_RES_Clr(); LCD_Close_Light(); // 屏最低功耗
      CST816_Sleep();                    // 触摸芯片睡眠（INT 仍可唤醒）
      vTaskSuspendAll();                 // 停调度器
      WDOG_Disnable();                   // 停外部看狗（IWDG 不可停，故用外狗）
      CLEAR_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk); // 关 tick 中断，否则 1ms 一次唤醒 STOP
      HAL_PWR_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFI);
醒后: SET_BIT(SysTick->CTRL, SysTick_CTRL_TICKINT_Msk);   // 恢复 tick
      HAL_SYSTICK_Config(SystemCoreClock/(1000U/uwTickFreq));
      SystemClock_Config();              // STOP 醒来时钟回 HSI，必须重配 PLL
      WDOG_Feed(); xTaskResumeAll();
```
**醒-查-再睡**：唤醒后不立即干活——`MPU_isHorizontal()` 抬腕判定（wrist_state UP/DOWN 状态机）或 `!KEY1 || KEY2 || ChargeCheck()` 才真醒来；否则 `goto sleep` 跳回睡前序列继续睡（RTC 0.98s 周期唤醒仅维持日历，绝大多数醒是假醒）。真醒来：MspInit 恢复 UART+LCD_Init+CST816_Wakeup+充电检查+发主页更新消息。

**唤醒源**（EXTI，睡眠期间唯一活动）：EXTI2=充电插入（双沿）、EXTI4=KEY2、EXTI9_5=KEY1、EXTI15_10=MPU6050 motion（抬腕唤醒硬件源头）、RTC_WKUP（0.98s 周期）。SysTick 已关、外设全睡——STOP 电流主要剩 LSE+RTC+EXTI。

**省电设计矩阵**：传感器按页面按需唤醒（心率页才 Enable EM7028、罗盘页才 Wakeup LSM303，离开即睡）；BLE 默认关（进蓝牙页才 Enable）；LvHandler 用 `lv_task_handler()` 返回值 clamp [1,30]ms 自适应 osDelay（空闲时 30ms 节拍省电，交互时 1ms 跟手）；`lv_disp_get_inactive_time(NULL)<1000` 当作用户活跃信号发 IdleBreak。

## 5. LVGL 集成

- **自适应节拍**：`wait=lv_task_handler(); clamp 1~30; osDelay(wait)`——LVGL 官方推荐，优于固定 5ms。
- **页面栈 PageManager**：Page_t{init/deinit/page_obj}+PageStack（MAX_DEPTH）；Page_Load/Back/Back_Bottom；Back 到空自动垫 Home+Menu；切换统一 `lv_scr_load_anim(MOVE_RIGHT,100,0,true)`。19 个 Screen 一套管理，页面职责=init 建控件/deinit 停定时器。
- **硬件抽象 HWDataAccess**：`HWInterface` 结构体=每外设一节（Init/Sleep/Enable/Get 函数指针+ConnectionError+数据缓存+wrist_state）；每函数 `#if HW_USE_XXX` 开关——**PC 模拟器把 HW_USE_HARDWARE 置 0，同一套 UI 代码跑电脑**（lv_sim_vscode_win 与板上 GUI_App 同源目录拷贝）。
- **flush 链**：LVGL flush_cb→`LCD_Color_Fill`（设窗口+`HAL_SPI_Transmit_DMA` 整块 RGB565）→`HAL_SPI_TxCpltCallback` 里 **`while(SR & SPI_FLAG_BSY)` 等移位寄存器最后一位**（DMA 完成≠总线发完，拉 CS 前必做）→`lcd_ready_cb()` 通知 LVGL（lcd_ready_cb 应 volatile——见 §9）。
- TickHook：`lv_tick_inc(1)`+IdleTimerCount 无操作计时+TimerPage 秒表（放 hook 保证实时）。

## 6. 显示与触摸驱动

ST7789（240×280 长条屏，GRAM 240×320 裁切，**OFFSET_Y=20** 居中——与 ST7735 160×80 offset=24 同族：长条屏开窗必须加行偏移）。硬件 SPI1 50MHz 模式 3（CPOL=1/CPHA=2EDGE），保留注释版软 SPI 可切换。初始化序列：RES 100ms×2→0x11(120ms)→0x36 方向(0x00/0xC0/0x70/0xA0)→0x3A=0x05(16bit)→0xB2 前后廊→0xB7=0x35→0xBB=0x19→0xC0/C2/C3/C4/C6 电源帧率→0xD0→0xE0/0xE1 gamma 各 14 字节→**0x21 IPS 反色**→0x29。SleepIn/Out=0x10/0x11+100ms。背光=TIM3 CH3 PWM `CCR=dc*300/100`（dc 5~100）。

CST816 电容触摸：软 I2C **对象化** `iic_bus_t`（引脚装结构体、函数收指针——比全局宏版可多设备共存）；XposH/YposH 连读 4 字节拼 12 位坐标；复位 RST 10ms+100ms；`CST816_Sleep/Wakeup` 配合系统睡眠。坑见 §9 优先级条目。

## 7. 电源管理

- **主电源自锁**：PA3 推挽输出，Power_Enable 拉高=MCU 自己保持供电（开机键只给脉冲）；关机=拉低（TIM1 ISR 里 KEY1 长按 3s→Power_DisEnable，1ms 时基计数 3000 次）。
- **电池测量**：BatCheck_8times 8 次均值+1ms 间隔；`V=raw*2*3.3/4096`（×2 分压）；**充电时减内阻压降 `INTERNAL_RES(0.128Ω)*CHARGING_CUR(1A)`≈0.128V 修正充电虚高**（产品级细节）；4.2~3.45V 11 段查表（100%~5%）。
- **KT6328**：单键开关机 IC，此处当蓝牙模块供电开关（Enable/Disable=PA8 高低）。主电源自锁（PA3）与蓝牙电源（PA8）独立。

## 8. 传感器链速记

- MPU6050+eMPL DMP：软 I2C PB10/11；计步器接口 dmp_get/set_pedometer_step_count；MPU INT motion 唤醒；`MPU_isHorizontal()` 抬腕判定的数据源。
- EM7028 心率：官方 .lib 三件（em70xx_bpm_dynamic/Blood_Process 系列），HR_Calculate(HRS1 原始,timecount) 出 BPM，50~120 才采信；测心率时 `vTaskSuspendAll` 包裹 lib 调用。
- LSM303DLH：加速度+磁力计→Azimuth_Calculate 航向角（负值+360 归一）。
- SPL06-001：气压高度计 Altitude_Calculate。
- AHT21：温湿度，数据合理性窗口（-10~50℃/0~100%）过滤坏值。
- BL24C02 EEPROM：设置项+跨日步数（SettingGet/SettingSave）。

## 9. bug 与现场妥协清单（读码发现，全部有源码依据）

1. **CST816 Y 坐标运算优先级**（CST816.c:118）：`((DAT[2]&0x0F)<<8)|DAT[3] + TOUCH_OFFSET_Y`——`+` 高于 `|`，TOUCH_OFFSET_Y=0 掩盖；正确写法应整体加括号。位运算与算术混用必须显式括号。
2. **pubsub.c 订阅链表用 malloc/free**：嵌入式运行期动态内存（本工程订阅发生在初始化期、不频繁 free，实际风险低）——纪律上应静态数组或 FreeRTOS 堆。
3. **lcd_ready_cb 非 volatile**（lcd.c:10）：DMA ISR 写、任务读，缺 volatile（validator 抓获，真实违规）。
4. **HardInt_receive_str 非 volatile**（stm32f4xx_it.c:69）：USART1 ISR DMA 写、任务读，缺 volatile（validator 抓获）。
5. SPO2 页面 `if(0)` 占位未实现（SensUpdateTask 里血氧分支空壳）。
6. LCD_Fill 与 LCD_Color_Fill 边界约定不一致：Fill 用 xend-1（闭区间 [xsta,xend-1]）、Color_Fill 用 xend（[xsta,xend]）——同文件两套约定，江协原版遗传。
7. IAP 跳转前未复位外设（只关 SysTick+全局中断），依赖 APP 侧 MX 重配。
8. HardwareInitTask 末尾 `vTaskDelete(NULL)` 后还跟 `osDelay(500)` 死码（坑 60 家族）。

## 10. validator 记录 + 生成检查单

**validator 增强**：新增 `--density f4`（F4 家族向量域：TIM1_BRK_TIM9/TIM1_UP_TIM10/TIM1_TRG_COM_TIM11 三合一、DMA1/2_Stream0~7、RTC_WKUP/TAMP_STAMP 等），F4 向量不再误报「未知中断处理函数名」；截断拼写检测并集同步扩展。回归：bad_sample 10V FAIL 不变、templates/main.c PASS 不变。

**75 文件实测**：5 VIOLATION——pubsub malloc/free×2（真）、lcd_ready_cb 非 volatile（真）、HardInt_receive_str 非 volatile（真）、iic_hal.c GPIO 初始化无本文件时钟使能（**误报**：时钟由调用方 CST816_GPIO_Init 先使能，跨文件顺序约定，单文件分析盲区）。

**生成检查单（可穿戴/低功耗/LVGL/IAP 场景）**：
1. STOP 前三件套齐了吗：vTaskSuspendAll + 关 SysTick TICKINT + 停看狗（外狗可停/IWDG 需	configPRE_STOP 处理）；醒后三件套：恢复 TICKINT + 重配 SystemClock + ResumeAll。
2. 唤醒后先判源再干活（醒-查-再睡），假醒立即回睡。
3. Boot→APP 跳转五件套逐条对照；APP 侧 VTOR 与 Keil IROM1 起址同步改。
4. 升级流程先清"有效标志"再写固件区，最后写标志（掉电安全）。
5. LVGL 节拍用 lv_task_handler 返回值自适应；TickHook 里 lv_tick_inc。
6. 传感器按页面/按需唤醒，测量窗口外全部 Sleep。
7. SPI DMA 完成回调里等 BSY 清零再拉 CS/通知完成。
8. 与 ISR 共享的缓冲（UART DMA 接收buf、回调标志）一律 volatile。
9. 硬件抽象层+HW_USE 开关，UI 代码 PC/板同源。
10. 电池曲线按具体电芯查表，充电态做内阻压降修正。
