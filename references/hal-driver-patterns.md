# HAL 第三方驱动范式（基于 Mculover666/HAL_Driver_Lib）

> 基准来源：<https://github.com/Mculover666/HAL_Driver_Lib>（BSD-3-Clause，© 2022 Mculover666），
> 本机镜像 `D:\stm32project\HAL_Driver_Lib`（生成代码时可直接对照）。
> 覆盖 18 个驱动：OLED、AT24C02、BH1750、DS18B20、SGP30、HTS221、W25QXX-SPI/QSPI、W5500、HC-SR04、DW1000、LCD(FMC/FSMC/LTDC/SPI)、SDRAM、KEY、LED。
> 本文提炼其**共性写法**为范式，供生成 HAL 轨传感器/存储驱动时对照。
> LCD 四总线/SDRAM/QSPI/DMA2D 器件级细节与 bug 清单 → display-storage-hal.md（配套阅读）。

## 1. 通用结构：三层分离

每个驱动 = **移植层（static 底层函数） + 协议层（器件命令时序） + 应用层 API**：

```c
/* oled.c —— 移植层：全工程只有这两个函数碰 HAL，移植只改这里 */
static void OLED_Write_Cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};              // 控制字节 0x00=命令
    HAL_I2C_Master_Transmit(&hi2c1, 0x78, buf, 2, 0xFFFF);
}
static void OLED_Write_Dat(uint8_t dat) {
    uint8_t buf[2] = {0x40, dat};              // 控制字节 0x40=数据
    HAL_I2C_Master_Transmit(&hi2c1, 0x78, buf, 2, 0xFFFF);
}
/* 上面是唯一依赖 HAL 的地方，其余全是纯器件逻辑 —— 换平台/换引脚不动协议层 */
```

**生成代码时沿用此结构**：底层收发收敛为 1~2 个 static 函数，业务函数不直接调 HAL。

## 2. 句柄注入的两种写法

| 方式 | 例 | 适用 |
|---|---|---|
| 直接 extern（CubeMX 变量） | `HAL_I2C_Master_Transmit(&hi2c1, ...)`（oled.c，`#include "i2c.h"`） | 单实例、快速开发 |
| 宏配置（可改名） | `#define BH1750_I2C_HANDLE hi2c1`（bh1750 头文件） | 多实例/换板只改宏 |

BH1750 驱动另示范了**多平台条件编译**（`#define USE_ST_HAL` / `USE_NXP_FSL` 二选一）——做通用驱动时值得学。

## 3. I2C 器件两类时序

### 3.1 命令型（BH1750 0x46/0x47，7bit 地址 0x23）
```c
/* 发命令：写 1 字节指令码 */
HAL_I2C_Master_Transmit(&BH1750_I2C_HANDLE, BH1750_ADDR_WRITE, &cmd, 1, 0xFFFF);
/* 读结果：直接收 */
HAL_I2C_Master_Receive(&BH1750_I2C_HANDLE, BH1750_ADDR_READ, buf, 2, 0xFFFF);
```

### 3.2 存储型（AT24C02 EEPROM，写地址 0xA0 / 读 0xA1）
```c
/* 写：器件地址 + 存储地址 + 数据 连发（EEPROM 容量小、地址 8bit，不用 Mem API） */
uint8_t buf[2] = {(uint8_t)addr, dat[0]};
HAL_I2C_Master_Transmit(&hi2c1, AT24C02_ADDR_WRITE, buf, 2, 0xFFFF);
HAL_Delay(WRITE_CYCLE_TIME_MS);   /* ⭐ 写周期 5ms：写后必须等，期间读会失败 */
/* 大容量(>256B 页地址超 8bit)才用 HAL_I2C_Mem_Read/Write，DevAddress 填 (0xA0>>1)=0x50、MemAddress 16bit */
```
注意：AT24C02 的 `Read_Amount_Byte` 是「写 1 字节存储地址（无 STOP）→ repeated START 读」，
对应 HAL 的 `HAL_I2C_Mem_Read(&hi2c1, 0xA1, addr, I2C_MEMADD_SIZE_8BIT, buf, size, t)`。

## 4. SPI 器件（W25Q64）：接口收敛为 5 个函数

```c
int      w25qxx_init(void);                                    /* 读 ID 验证 */
uint16_t w25qxx_read_deviceid(void);
int      w25qxx_read(uint8_t* buf, uint32_t addr, uint16_t n);
int      w25qxx_page_program(uint8_t* dat, uint32_t addr, uint16_t n);  /* 页编程 ≤256B */
int      w25qxx_erase_sector(uint32_t sector_addr);            /* 擦除 4KB 扇区 */
```
写法：CS 手动拉低（`HAL_GPIO_WritePin`）→ `HAL_SPI_TransmitReceive` 换命令+地址 → 数据 → CS 拉高；
返回 int 错误码（0 成功），**写前必须先擦**（Flash 特性）。QSPI 版在 W25Q64-QSPI（F4/H7 用）。

## 5. 单总线 + 微秒延时：DS18B20 与 core_delay（⭐ 重点沉淀）

DS18B20 目录自带 `core_delay/`（野火出品）——**DWT 周期计数微秒延时**，F1(CM3) 同样可用：

```c
#define DWT_CR      *(__IO uint32_t *)0xE0001000
#define DWT_CYCCNT  *(__IO uint32_t *)0xE0001004
#define DEM_CR      *(__IO uint32_t *)0xE000EDFC
#define DEM_CR_TRCENA            (1 << 24)   /* DEMCR.TRACENA：使能调试外设 */
#define DWT_CR_CYCCNTENA         (1 <<  0)   /* DWT_CTRL.CYCCNTENA：使能周期计数 */

/* 初始化：使能→清零→开计数；之后 CYCCNT 每内核时钟 +1（72MHz 下 1us=72 计数） */
```

**更有价值的是它重写了 `HAL_InitTick()`**：把 HAL 时基从 SysTick 迁到 DWT 计数——
- 裸机 HAL 工程：SysTick 完全释放（不用像 CubeMX 那样占一个 TIM）；
- HAL+FreeRTOS 工程：HAL tick 与 RTOS tick 各走各的（DWT vs SysTick），天然无冲突，比「时基迁 TIM」还省一个定时器。
- 限制：DWT 是调试外设，正式量产对时基可靠性要求苛刻的场合仍建议用 TIM 方案；且内核停机调试时计数暂停。

DS18B20 本体：`ds18b20.c/h` 只有 3 个 API（init / 读温度 float），单总线时序全部用 core_delay 的 us 级延时实现。

## 6. 传感器读数范式补充（SGP30 / HTS221 / DS18B20 / HC-SR04 实测细节）

### 6.1 Sensirion 命令+CRC 型（SGP30）
- 软复位用 I2C **通用调用地址 0x00**+0x06：总线上有其他响应 general-call 的器件会被一起复位（谨慎）。
- 每条命令 2 数据字节跟 1 字节 **CRC8：poly 0x31、init 0xFF**（Sensirion 全家同款，读回必须校验）。
- `init_air_quality` 后每 ≥1s 轮询一次；**前 15s 固定返回 CO2=400ppm**（手册 warm-up 行为，非故障）；持续断电会丢 baseline，恢复需重新 15s。

### 6.2 出厂校准线性插值（HTS221）
```
T0/T1 degC：0x32/0x33 存 ×8 值，0x35 高 2 位补小数 → T/8
T0_out/T1_out：0x3C/0x3D 与 0x3E/0x3F 各一组有符号 16bit
T = T0 + (T_out − T0_out) × (T1 − T0) / (T1_out − T0_out)
湿度同构（H0_rH/H1_rH 为 ÷2 值）
```
⚠️ 本库实现有 bug（0x3C~0x3F 分两次读进同一对 buffer[0]/[1] 先后被覆盖、合成却取 buffer[3]<<8|buffer[2] 栈垃圾），引用时必须改：**每组校准寄存器读入独立变量并立即合成**。

### 6.3 DS18B20 时序常量（实测库值）
复位：拉低 750µs→释放 15µs→存在检测双段各 240µs 超时；写槽 2µs+60µs；读槽拉低 2µs→10µs 采样→60µs 收尾。0xCC Skip ROM→0x44 转换→忙等读 0xFF→0xBE 读 scratchpad→0.0625℃/LSB（×0.0625 转 float）。

### 6.4 HC-SR04（对象化模板）
`hc_sr04_device_t{trig_port/pin, echo_port/pin, tim, distance}`——每探头一个实例，echo 宽度用 TIM 计数测。⚠️ 双 `while` 等 echo 起止**无超时**（探头缺失挂死）；库预留 Mutex pend/post 为 RTOS 钩子。

## 7. 通信协议栈移植层范式（W5500 / DW1000）

### 7.1 W5500：ioLibrary 八回调移植（w5500_port_hal.c）
ioLibrary 只认八个符号，porting = 实现这八个：
```
cris_enter/exit   —— __set_PRIMASK(1)/__set_PRIMASK(0) 临界区（库内部中断安全）
cs_select/deselect—— SPI CS GPIO
spi_readbyte/writebyte、readburst/writeburst —— HAL_SPI_*（各 1000ms 超时）
```
再补：硬复位（RST 低 50ms 高 10ms）→ `wizchip_init(NULL,NULL)`（默认 2KB×8 socket 缓冲）→ PHY 自协商/手动 → `setnetinfo`（mac/ip/sn/gw/dns/dhcp）→ 库回调注册。**这套顺序 = W5500 出网模板**。

### 7.2 DW1000：SPI 头/体双缓冲 + 保护区 + 变速
- `writetospi/readfromspi`：头（寄存器地址+子地址）与数据体分开传缓冲——Decawave 驱动约定的签名，别合并。
- `decamutexon/off`：进/出 SPI 访问的临界区（返回先前状态恢复）。
- `SPI_ChangeRate`：初始化阶段 /32 低速 → 运行 /4 高速，直改 CR1 波特率位（0xFFC7 掩码清位重填）。
- reset_DW1000：RSTn 拉低后转输入浮空 2ms（非保持输出）。

## 8. OLED：与技能已有文档的对照

| | 江协例程（SPL 轨基准，见 oled-ssd1306.md） | 本库 OLED（HAL 轨参考） |
|---|---|---|
| 总线 | 软件 I2C（GPIO 模拟，引脚宏） | **硬件 I2C**（`hi2c1`，从机地址 0x78） |
| 坐标 | Line 1~4 / Column 1~16（字符界面坐标） | x=0~127 列 / y=0~7 页（物理坐标） |
| 字模 | `OLED_F8x16[][16]` const，索引 `chr-' '` | oledfont.h（16/8 两套 size 参数） |
| 移植点 | 改 GPIO 宏 | 改 `OLED_Write_Cmd/Dat` 两个 static 函数 |

### ⚠️ 本库驱动发现的可疑代码（引用第三方前必须审查的实例）
```c
void OLED_Set_Pos(uint8_t x, uint8_t y)
{   OLED_Write_Cmd(0xb0+y);
    OLED_Write_Cmd(((x&0xf0)>>4)|0x10);
    OLED_Write_Cmd((x&0x0f)|0x01);   /* ⚠️ 手册为 0x00|(x&0x0f)，|0x01 强制 bit0=1，偶数列全偏移为奇数 */
}
```
SSD1306 列地址低 4 位命令是 `0x00~0x0F`（本底 0x00），`|0x01` 是错写。若显示「整体右移半列/偶数列丢失」先查这里。
**教训**：第三方驱动入库前，对照芯片手册核对每条命令公式，别默认作者写过板验证。

## 9. 命名与风格流派（生成代码保持与工程内一致）

| 流派 | 例 | 特征 |
|---|---|---|
| 模块_动作 | `AT24C02_Write_Byte`、`OLED_ShowString` | 国内例程主流，好读 |
| 模块_动作(小写) | `w25qxx_page_program`、`bh1750_read_light` | Linux 风格，返回 int 错误码 |

错误码约定（本库）：0=成功，负数=失败（`int` 返回）；生成存储/传感器驱动时建议沿用，调用方可判错。

## 10. 生成代码检查单（HAL 传感器/存储驱动）

1. 移植层收敛为 static 底层函数（§1），换板不改业务层
2. I2C 从机地址写/读分开宏定义（0xA0/0xA1 式），注释 7bit 原始地址
3. EEPROM/Flash 写后加写周期延时（AT24C02=5ms，W25Q 页编程+读忙）
4. 超时用 `0xFFFF` 或明确毫秒数，别用 0（HAL 立即返回超时）
5. 微秒时序（单总线/软件 I2C）用 DWT core_delay，禁裸 SysTick 方案（与 RTOS 冲突）
6. 字模数组必须 `const`（放 Flash，1.5KB/套）
7. 引用第三方驱动：核对寻址/命令公式（§8 Set_Pos 案例）、检查编码（GBK→转 UTF-8）、跑 validator；LCD/存储类先读 display-storage-hal.md §8 bug 清单
8. 通信协议栈移植（W5500/DW1000/LWIP）：按 §7 回调清单逐个实现，临界区用 PRIMASK 而非关中断总开关（RTOS 下更安全）
