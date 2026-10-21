# OLED (SSD1306 0.96" 128×64) 驱动知识

> 基准实现：江协科技例程（软件 I2C 4 针版 / 软件 SPI 7 针版），已在多工程验证。
> 本文档沉淀驱动原理、初始化序列、API 约定与移植方法——生成代码时按此写，不逐字复制第三方源码。
> 适用 SPL 与 HAL 双轨（写引脚函数替换即可，协议层完全相同）。

## 1. 硬件与引脚

| 接口 | 引脚 | 江协默认接法（F103） | GPIO 模式 |
|---|---|---|---|
| **4 针 I2C** | VCC GND SCL SDA | SCL=PB8, SDA=PB9 | **Out_OD 开漏**（模块自带上拉；引线长/多从机时外加上拉 4.7k） |
| **7 针 SPI** | GND VCC D0 D1 RES DC CS | D0/SCL=PB12, D1/MOSI=PB13, RES=PB14, DC=PB15, CS=PA8 | Out_PP 推挽（CS 帧内传输） |

## 2. 显存与坐标模型

- 128×64 = **8 页（页 0~7）× 128 列**，每字节 = 一列竖 8 像素，**D0 在上**；
- 字符 8×16：占**上下两页**各 8 字节（上半 8 + 下半 8），即 1 字符 8 列 → 屏幕共 **4 行 × 16 列**；
- 行列→页/列换算：`页 = (Line-1)*2` 与 `(Line-1)*2+1`，`列 = (Column-1)*8`；
- 设光标三条命令：`0xB0|页`、`0x10|列高4位`、`0x00|列低4位`。

## 3. I2C 版协议要点

- 从机写地址 **0x78**（8 位格式，即 0x3C<<1；个别模块 0x7A）；
- 控制字节：**0x00=命令流、0x40=数据流**（单条写法：START → 0x78 → 0x00/0x40 → 1 字节 → STOP）；
- 软件 I2C **可不处理 ACK**（额外给一拍时钟即可）：SSD1306 是纯从机、无时序强制，不读应答不会死等；
- 软件 I2C 位操作无需延时函数：72MHz 下 GPIO_WriteBit 翻转速率恰在 SSD1306 I2C 上限（400kHz）内；
- 换硬件 I2C1 时注意：I2C1 默认 PB6/PB7，重映射后正是 PB8/PB9——**与软件版默认脚撞车**，二选一。

## 4. SPI 版要点（7 针）

- 每帧：CS 拉低 → **DC 拉低=命令 / 拉高=数据** → 移位 8 位（D0 时钟、D1 数据）→ CS 拉高；
- RES 复位脉冲：上电 `RES(0)→延时≥3µs→RES(1)→延时≥120µs` 再发初始化（花屏/不亮先查这条）；
- 上电后、初始化前总延时 ≥100ms（等 VCC 稳定）；

## 5. 初始化命令序列（初始化 = 延时 → 引脚 → 逐条命令 → 清屏）

| 命令 | 参数 | 作用 | 可调 |
|---|---|---|---|
| 0xAE | – | 关显示（配置期间必须） | |
| 0xD5 | 0x80 | 显示时钟分频/振荡频率 | |
| 0xA8 | 0x3F | 多路复用率 63（64 行） | 32 行屏改 0x1F |
| 0xD3 | 0x00 | 显示偏移 | |
| 0x40 | – | 显示起始行 | |
| 0xA1 / 0xC8 | – | 段/COM 扫描方向（0xA1+0xC8=正常，反了图像镜像） | |
| 0xDA | 0x12 | COM 引脚配置（64 行用 0x12） | |
| 0x81 | 0xCF | 对比度（0x00~0xFF） | ★ |
| 0xD9 | 0xF1 | 预充电周期 | |
| 0xDB | 0x30 | VCOMH 电平 | |
| 0xA4 | – | 恢复 RAM 内容显示（0xA5 全亮测试用） | |
| 0xA6 | – | 正常显示（0xA7 反色） | |
| **0x8D** | **0x14** | **充电泵开启**（不写屏幕不亮，最常见故障） | ★ |
| 0xAF | – | 开显示 | |

## 6. API 约定（江协风格，生成代码沿用此签名）

```c
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);       /* Line 1~4, Column 1~16 */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
```

- `Length` = 显示位数，**高位补 0**（如 ShowNum(...,5,3) 显示 005）；
- 数字用「除 10^k 取余」逐位转字符，不依赖 sprintf（省栈、MicroLIB 友好）；
- 字模 `const uint8_t OLED_F8x16[][16]`，索引 `Char - ' '`，**必须 const**（95 字符×16B ≈ 1.5KB，
  无 const 会白吃 20K RAM 的 7.5%）。

## 7. 移植清单

**换引脚（软件版）**：改两个写引脚宏 + `*_I2C_Init` 里的 RCC 时钟/两处 GPIO_Pin，其余不动；
**HAL 轨对照**：`RCC_APB2PeriphClockCmd` → `__HAL_RCC_GPIOB_CLK_ENABLE()`；
`GPIO_WriteBit(GPIOB,GPIO_Pin_8,(BitAction)x)` → `HAL_GPIO_WritePin(GPIOB,GPIO_PIN_8,GPIO_PIN_SET/RESET)`；
`GPIO_Mode_Out_OD` → `GPIO_MODE_OUTPUT_OD`；协议层三条写函数零改动；
**硬件 I2C 版**：EV 事件轮询模板（见 peripheral-recipes.md §6）按「地址 0x78 → 控制字节 → 数据」三段连发；
**RTOS 工程**：`OLED_Init()` 放调度器启动前；驱动为阻塞型，全屏刷新约 40~80ms，只能在任务里调，禁止 ISR。

## 8. 常见坑

1. **上电不亮三查**：初始化前延时够不够（≥100ms）→ 充电泵 0x8D+0x14 写没写 → SCL/SDA 接反没；
2. 显示镜像/上下颠倒：0xA1/0xC8 方向命令与模块安装方向不匹配；
3. I2C 地址写 0x3C（7 位格式）导致整条链路无响应——软件版要发 **0x78**；
4. 字模数组漏 const → 编译能过但 RAM 紧张工程莫名爆栈（1.5KB 被搬进 RAM）；
5. 软件 I2C 引脚配成推挽：单模块短线能跑，接摄像头等多从机排线即挂——规范 Out_OD；
6. 刷新整屏用 `OLED_Clear` 后再画会闪：局部更新用 ShowString 覆盖原位置（等宽字符天然支持覆盖）；
7. RTOS 里把 OLED 刷新放多个任务无互斥 → 内容串行错位；单任务持有或加互斥锁。
