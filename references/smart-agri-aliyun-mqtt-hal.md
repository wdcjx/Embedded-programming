# 智能农业双子工程：阿里云 IoT MQTT + coreJSON（HAL 轨实测基准）

> 基准工程：STM32-G（监测节点 STM32_WIFI_1）+ STM32_MAKE（执行节点 STM32_WIFI_2），作者「七天睡七夜」V1.0 2024-04。
> F103C8T6 + CubeMX HAL + Keil MDK，同一套「User 层模板」复制的两个节点。**技能库首见：AWS coreJSON 嵌入式解析器 + 阿里云 IoT 平台 AT 接入**。
> 状态：作者实测上板（两节点云上互见）。validator 38 文件 4 FAIL（全真实，见 §10）。

## 1. 系统架构：一产品两设备

```
阿里云 IoT 平台（产品 k19hbks7W5c / k12hlTbQD38）
   │  MQTT 1883（ESP-AT 固件内置 MQTT 客户端）
   ├── STM32-G「监测」：Soil 土壤湿度 ADC + 5516 光敏 + DHT11 + SGP30(CO2/TVOC)
   │     上报：JSON_FORMAT1~3 → /sys/<pk>/<dev>/thing/event/property/post
   └── STM32_MAKE「执行」：fan/hot 继电器 + 舵机 TIM4 + LED + 电压 ADC
         订阅：/sys/<pk>/<dev>/thing/service/property/set（下行 JSON 命令）
         另有 USART1 手动模式（单字符 l/e/d 命令）
```

- 两节点 = **同一份工程模板复制**（oled/esp8266/core_json 三件套逐字节同源，User 业务层不同）——「模板复制出节点」是个人/课设 IoT 的主流做法，维护时修 bug 要修 N 份。
- 云端物模型：上报 `{"params":{...}}`、下行同名 key（`params.led` 等）——阿里 thing 模型标准格式。

## 2. 阿里云 IoT 接入（ESP-AT MQTT 命令集）

四步连接序列（`esp8266_init()`，每步 `while(receive_start==0 && count<1000)` 等回包+1000 次超时）：

```
AT+MQTTCLIENTID=0,"<clientId>|securemode=2\,signmethod=hmacsha256\,timestamp=<ts>|","<user>"
AT+MQTTCONN=0,"<productKey>.iot-06z00gs9rinyera.mqtt.iothub.aliyuncs.com",1883,1
AT+MQTTSUB=0,"<subTopic>",1
```

- **clientId 尾巴的 `securemode=2\,signmethod=...` 是阿里动态注册三元组格式，逗号必须 `\,` 转义**（AT 命令参数分隔符冲突），丢 `\` 连接必失败。
- 密码 = 设备密钥 HMAC-SHA256（阿里控制台算好填入宏）；user = `<deviceName>&<productKey>`。
- 上报：`AT+MQTTPUB=0,"<topic>","{\\\"params\\\":{\\\"temp\\\":%d\\,\\\"humi\\\":%d}}",0,0` —— **sprintf 拼一条完整 AT+JSON 命令，JSON 内引号要 `\\\"` 双层转义**（C 字符串+AT 命令两层），`%d` 后逗号也要 `\\,`。这是最容易拼错的一行，逐字符核对。
- 断线自愈：无——`esp8266_send_msg*` 失败只打印，不重连（见 §9 改进）。

## 3. coreJSON：零拷贝 JSON 解析（44K 单文件零依赖）

AWS/FreeRTOS 官方 coreJSON 库，F103C8 20KB RAM 装得下（无 malloc，全部栈/调用方缓冲）：

```c
JSONStatus_t result;
char query[] = "params.led";                 // 点分路径查询
char *value; size_t valueLength;
result = JSON_Validate((const char*)msg, len);            // 先整体验证
if (result == JSONSuccess)
  result = JSON_Search((char*)msg, len, query, sizeof(query)-1,
                       &value, &valueLength);             // 返回指针+长度，不拷贝
if (result == JSONSuccess) {
  char save = value[valueLength];           // 零拷贝手法：暂存结尾
  value[valueLength] = '\0';                // 就地截断
  led_status = atoi(value);                 // 用完
  value[valueLength] = save;                // 恢复（缓冲还要给下一次接收用）
}
```

- API 就三个常用的：`JSON_Validate`（全文校验）、`JSON_Search`（点分 query 定位，找到即停不校验剩余）、`JSON_Iterate`（遍历）。
- `JSON_MAX_DEPTH` 默认 32 层；查询键找不到返回 `JSONNotFound`。
- 选型对照：cJSON（树+malloc，RAM 紧张炸堆）vs coreJSON（流式零拷贝，只查不改）——**云命令解析这种「只取一两个字段」场景 coreJSON 是正解**；要构造 JSON 上报用 sprintf 模板比树 API 省得多。
- 陷阱：`JSON_Search` 的 query 带 `\0` 结尾会被计入长度——务必 `sizeof(query)-1`。

## 4. 传感器层

| 传感器 | 接口 | 实现要点 |
|---|---|---|
| 土壤湿度 Soilhua | ADC1 DMA 双通道（soil+光敏 5516） | `HAL_ADC_Start_DMA(&hadc1,(uint32_t*)adcData,2)` 循环模式；`Get_Adc_Average(10)` 十次均值；读数 /100 得「百分比」粗糙定标 |
| DHT11 | 单总线 GPIO 软时序 | `HAL_Delay(20)` 拉低 18ms+`delay_us` 自旋；**每个等待段 retry<100 次超时保护**（探头缺失不死等——比 HC-SR04 家族守规矩）；`delay_us` 为空循环自旋（72MHz 未标定，靠 DHT11 容差） |
| SGP30 | I2C **双实现 `#if` 切换** | 软 I2C 版（完整位拆）+ 硬件 I2C 版（`HAL_I2C_Master_Transmit(&hi2c2,...,100)`）；**硬件版 `sgp30_iic_init()` 内部自己调 `MX_I2C2_Init()`**——驱动越权调 CubeMX 生成函数（坑 93） |
| 电压检测 | ADC 轮询 | `ADC_battery_voltage`/`shine_voltage` 两路，与土壤共用 adcData 缓冲模式 |

- DHT11 读时序：等待应答 80µs→50µs→每 bit 起始 50µs+数据 26/70µs（`delay_us(40)` 采样点判 0/1）——**40µs 采样是全协议的临界点**。

## 5. 执行层（MK 节点）

- **继电器低有效**：`fan/hot_switch(1)=WritePin(RESET)`——常见继电器模块低电平吸合，与 LED 高有效相反；两路对称封装。
- **舵机**：TIM4_CH1 50Hz，`Servo_Control(angle){ CCR = angle/9*10+50; }` → 0~180° 映射 0.5~2.5ms 标准脉宽（整数运算避浮点，`angle/9*10` ≈ angle*1.11）。
- **手动/自动双模式**：`mode_status==0` 时 USART1 单字符命令直控（`l` 洒水开/`e` 关/`d` 补光……），云端 JSON 下行走另一分支——**两条控制路径都改同一组 status 变量**，状态机收敛在输出端（每轮循环 `hot_switch(hot_status)` 重放）。

## 6. 调度：TIM2 10ms 时间片 + 溢出判定

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == htim2.Instance) {
    index_10ms++;
    if (index_10ms % 50 == 0)  index_DATAreward = 1;   // 500ms 采集
    if (index_10ms % 100 == 0) { index_led=1; index_oled=1; index_dht11=1; }  // 1s
    if (index_10ms >= 500) { index_send_msg = 1; index_10ms = 0; }            // 5s 上报
  }
}
```

- 同文件还判 `htim->Instance==TIM1`（HAL 时基）——**一个弱回调按 Instance 分发多定时器**的标准写法（与 hub75d 时钟同款）。
- 标志全 `uint16_t` 非 volatile（validator 6+8 条真实 VIOLATION）；M3 对齐单读原子+% 判定读撕裂无害——能跑，纪律不及格。
- **5000 帧软复位自愈**：OLED 计数 `count>5000 → __set_FAULTMASK(1)+NVIC_SystemReset()`——无人值守设备「定时重启保活」的暴力实现。能用（IoT 挂死自愈），但 FAULTMASK 并非复位必需（SystemReset 不受可屏蔽中断影响），语义是「防止复位窗口被中断打断复位前的状态操作」——如实按手法记录，新写建议直接 `NVIC_SystemReset()`。

## 7. 串口接收：USART2 512B 状态机

- ESP 接收：`receive_buf[512]` + `receive_start/receive_count/receive_finish` 三变量状态机（HAL_UART_RxCpltCallback 逐字节累计+判帧尾）。
- 手动模式 USART1：`HAL_UART_Receive_IT(&huart1,&data,1)` 单字节——**重挂写在 RxCpltCallback 的每个 switch-case 分支里**（漏一个 case 串口就断流，坑 94）。
- `uart_printf.c`：fputc 重定向 172B 标准件。

## 8. OLED 与模板件

- oled.c 硬件 I2C1 `HAL_I2C_Master_Transmit` 阻塞 10ms 超时；`OLED_printf(行,列,fmt,...)` vsprintf 变体；oledfont 12KB 常量表。
- 两工程 oled/core_json/uart_printf 逐字节同源（模板复制）。

## 9. bug 清单与改进

1. `HAL_UART_RxCpltCallback` 里直接 `OLED_printf`（坑 68 家族第四例）——ISR 刷屏。
2. Receive_IT 重挂在每个 case 里重复（坑 94）。
3. `sgp30_iic_init` 内嵌 `MX_I2C2_Init()`（坑 93）。
4. 无断线重连：MQTT 断后 send 只报错不重连（对照 led32x64 的 2 分钟兜底断电）。
5. 5000 帧软复位无日志无计数器落盘——重启原因不可考（清零重启计数是基本可观测性）。
6. JSON 模板 `\\,` 转义手拼无单元测试（离线 sprintf 断言一条就能钉住格式）。
7. index 标志非 volatile ×14（validator 全数抓出）。
8. DHT11 `delay_us` 未标定（依赖编译器不优化空循环，-O2 即翻车）。

## 10. validator 记录（v1.1.0，--density md）

38 文件：4 FAIL 全真实零误报——
- G/MK time_handle.c R3×6/×8（ISR 标志非 volatile）
- MK main_program.c R3×8（RxCpltCallback 8 状态变量）
- G dht11.c R10（GPIO 初始化无本文件时钟——HAL 生成层 gpio.c 已开，CubeMX 生成层模式的已知边界，非缺陷）
- WARNING：esp8266.c printf 重定向在 uart_printf.c（跨文件）×3。

## 11. 生成检查单（本域代码生成时）

1. AT+MQTTPUB 的 JSON 引号/逗号转义（`\\\"`/`\\,`）逐字符核对，交付附一条实测样例。
2. coreJSON 查询用 `sizeof(query)-1`，零拷贝取值遵循「存-断-用-复」四步。
3. clientId 尾巴 `securemode=2\,...` 的 `\,` 转义别丢。
4. Receive_IT 重挂写在回调出口（switch 之后统一一次），不进 case。
5. 时间片 ISR 标志 volatile + 定宽类型（uint8_t 溢出回绕比 % 判定更稳）。
6. 断线场景：至少重连计数+上报失败退避，比静默打日志强。
7. 舵机角度→CCR 用整数映射式（angle*11/10+50）并注明脉宽范围。

## 12. 与既有文档的关系

- ESP8266 另两种用法：hub75d-wifi-clock-hal.md（AT+USERTIME 校时）/ README 首屏大棚案例（HTTP 透传）——本文档是第三种（MQTT 物模型），三分支互补。
- SGP30 CRC/时序细节：hal-driver-patterns.md §6（本文档不重复）。
- 软 I2C 位拆标准件：多工程同款，见 portable-lib-design.md §位域。
