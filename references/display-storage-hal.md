# 显示与存储外设（HAL 轨：LCD 四总线 / SDRAM / QSPI / DMA2D）

> 基准来源：Mculover666/HAL_Driver_Lib 本机镜像 `D:\stm32project\HAL_Driver_Lib`（BSD-3-Clause）。
> 本文接续 hal-driver-patterns.md（共性范式），沉淀**四种 LCD 总线形态、FMC-SDRAM、QSPI Flash** 的器件级实现，以及该库实测发现的 bug 清单（引用前必读 §8）。
> validator 结果：23 个驱动文件 21 PASS；led_drv.c（宏封装时钟→修复后 WARNING）、dw1000.c（时钟确在 main.c，真 VIOLATION）。

## 1. LCD 四总线总览（同库四种形态，选型即查）

| | LCD-SPI | LCD-FSMC / LCD-FMC | LCD-RGB-LTDC |
|---|---|---|---|
| 典型面板 | ST7789 1.3" 240×320 MCU 屏 | ILI9341/NT35310/NT35510 并口 MCU 屏 | RGB 裸屏（F429/F7 LTDC） |
| 数据通路 | SPI（CS/DC/RST/BLK） | 8080 并口挂 FMC/FSMC 总线 | LTDC 引脚直驱 + 像素时钟 |
| 显存位置 | **屏内 GRAM** | **屏内 GRAM** | **MCU 侧帧缓冲（SDRAM）** |
| 刷新方式 | 写窗口命令 0x2A/0x2B/0x2C + 数据流 | 写 `LCD->lcd_reg / lcd_ram` 两个地址 | CPU/DMA2D 直写帧缓冲，LTDC 硬件扫描 |
| 驱动要做的事 | 发命令序列 + 窗口 + 像素流 | 同上（地址映射替代命令/数据引脚切换） | 无命令序列；纯内存操作 + LTDC 时序配置（CubeMX） |
| 速度瓶颈 | SPI 时钟（寄存器直写见 §2） | FMC 时序（快 10 倍以上） | 无总线瓶颈，DMA2D 加速 |
| 适用芯片 | 全系 | F1/F4=FSMC；F429/F7=**FMC**（仅更名+时钟树不同） | 仅带 LTDC 的 F429/F7/H7 |

**FMC vs FSMC**：同一外设两代名字。F1/F4 叫 FSMC，F429/F7 起叫 FMC（SDRAM 控制器增强）。LCD 并口驱动代码几乎通用，差别在 HAL 句柄类型（`SRAM_HandleTypeDef`/`FMC_SDRAM_CommandTypeDef`）与 CubeMX 配置页。

## 2. LCD-SPI（ST7789，lcd_spi_drv.c 精华）

**函数指针移植表**（比三层结构更彻底的移植层）：
```c
typedef struct { int (*cs)(int); int (*blk)(int); int (*rst)(int); int (*dc)(int);
                 int (*init)(void); int (*write_byte)(uint8_t);
                 int (*write_multi_bytes)(uint8_t*, uint16_t);
                 void (*delay)(uint32_t); } lcd_spi_drv_t;
static lcd_spi_drv_t lcd_spi_drv = { ... };   /* 平台相关，填一次 */
#define lcd (&lcd_spi_drv)                      /* 器件层全部走 lcd->xxx */
```
同一份器件层代码在 STM32 与 ESP32（esp-idf）双平台编译——换平台只重填结构体。

**性能实测（作者注释，L4@80MHz 240×240 清屏，-O3 -Otime）**：
| 写法 | 清屏耗时 |
|---|---|
| `HAL_SPI_Transmit` 单字节逐发 | 632 ms（320×240：741ms） |
| 寄存器直写：`while(!__HAL_SPI_GET_FLAG(&hspi2,TXE)); *((__IO uint8_t*)&hspi2.Instance->DR)=data;` | **30 ms**（320×240：55ms） |
| HAL 多字节 + 发送缓冲（`USE_SPI_WRITE_BUF`，预填色块缓冲循环发） | 75 ms @L5 110MHz |

结论：**刷屏热路径里 HAL 函数调用开销 > 一切**。量产驱动在 write_byte 层做寄存器直写（`SPI_1LINE_TX(&hspi2)+__HAL_SPI_ENABLE` 预置），DC/CS 用 GPIO 直写。注意 DR 必须 8 位访问（`(__IO uint8_t*)&DR`），16 位访问会引入 padding 字节。

**ST7789 初始化序列**（驱动完整实现）：硬复位（RST 低 100ms）→ `0x11` Sleep Out + 等 120ms → `0x36` MADCTL（0x00/0x60/0xA0/0xC0 四方向；ST7789 240 宽面板用 0x60/0xA0 特有值）→ `0x3A=0x05` RGB565 → `0xB2` Porch ×5 → `0xB7` Gate 0x72 → `0xBB` VCOM 0x3D(1.625V) → `0xC0/0xC2/0xC3/0xC4/0xC6` 电源与帧率 → `0xD0` Power1 → `0xE0/0xE1` Gamma 正负各 14 字节 → **IPS 屏必须 `0x21` 显示反转** → `0x29` Display On。
垂直滚动：`0x33` 定义滚动区（tfa+vsa+bfa 必须等于 320）+ `0x37` 设滚动起始行。
CS 可常拉低（`lcd_cs_port` 空实现）——单器件总线省去 CS 管理。

## 3. 8080 并口（lcd_fmc.c / lcd_fsmc.c，正点原子风格移植）

**地址映射**：RS(命令/数据) 引脚接 FMC 某根地址线（如 AX0），于是两个地址分别选中命令/数据模式：
```c
#define LCD_BASE  0x6C000000UL        /* FMC Bank1 NE1 区 */
typedef struct { volatile uint16_t lcd_reg; volatile uint16_t lcd_ram; } lcd_t;
#define LCD ((lcd_t *)LCD_BASE)
lcd_write_cmd(cmd)  -> LCD->lcd_reg = cmd;
lcd_write_data(dat) -> LCD->lcd_ram = dat;
```
写像素 = 往 `lcd_ram` 连续写 uint16——FMC 自动产生 8080 写时序，速度与写内存相同。

**MPU 配置（带 Cache 芯片必做，否则刷屏丢序/花屏）**：
```c
MPU_Initure.BaseAddress = LCD_ADDRESS_START;  /* FMC LCD 区域 */
MPU_Initure.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;   /* 禁 Cache */
MPU_Initure.IsBufferable = MPU_ACCESS_BUFFERABLE;     /* 允许写缓冲 */
```

**三控制器 ID 探测链**：`0xD3` 读 4 字节→ILI9341(0x9341)；`0xD4`→NT35310(0x5310)；`0xDA00/0xDB00/0xDC00` 三条命令拼→NT35510(0x5510)。探测结果存 `lcd_params`，后续命令表（set_x/y、wram）按控制器切换——**一份驱动兼容多屏**的正确做法。
八方向扫描：`lcd_set_scan_direction()` 按方向重映射 MADCTL 三位（MY/MX/MV），横屏时交换宽高并翻转方向枚举。

## 4. RGB-LTDC + DMA2D（lcd_rgb_ltdc_drv.c）

无命令序列——**驱动 = 帧缓冲内存操作**（LCD_FRAME_BUFFER 宏指向 SDRAM）：
- `lcd_draw_point`: `fb[y*LCD_WIDTH + x] = color;`（越界 return）
- LTDC 引脚/时序/层配置全在 CubeMX（main.c），驱动只管像素

**DMA2D 寄存器级使用**（不走 HAL_DMA2D，更短更快）：
```c
/* 矩形填充 R2M（register to memory）*/
DMA2D->CR = DMA2D_R2M;  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
DMA2D->OCOLR = color;   DMA2D->OMAR = (uint32_t)addr;
DMA2D->OOR = offsetLine;                    /* ⭐ 行偏移 = 屏宽-矩形宽，跳过行尾 */
DMA2D->NLR = (xSize<<16) | ySize;           /* 像素数：高<<16 | 宽 */
DMA2D->CR |= DMA2D_CR_START;  while (DMA2D->CR & DMA2D_CR_START);
/* 整块搬运 M2M：OMAR=目的 FGMAR=源，OOR/FGOR 各自行偏移 */
```
`lcd_clear` 全屏 = 一次 R2M；`lcd_fill_with_buffer`（图片/截图写入）= 一次 M2M。

**GB2312 汉字字模偏移**（全库 hz16/24/32 取字）：
```c
/* 区码 qh 位码 ql（GB2312 双字节 >=0xA1）*/
符号区 (0xA1..0xA9): offset = (94*(qh-0xA1) + (ql-0xA1)) * font_bytes;
汉字区 (0xB0..0xF7): offset = (94*(qh-0xB0) + (ql-0xA1) + 846) * font_bytes;  /* +846 跳过符号区 */
```
混合字符串 `lcd_show_str`：`<0x80` 走 ASCII（宽 size/2），否则按双字节汉字（宽 size）。
全字库代价：hz16=1.3MB、hz24=3.3MB、hz32=5.9MB——**必须外挂 SPI Flash/SDRAM 存放或裁剪**（库提供 `USE_CHINESE_FULL_LIB` 开关切换全库/自选 `font_hz.h`）。

## 5. FMC-SDRAM（W9825G6KH 32MB，sdram_fmc_drv.c 全量 65 行）

初始化六步（HAL_SDRAM_SendCommand 封装在 static SDRAM_SendCommand）：
1. `FMC_SDRAM_CMD_CLK_ENABLE`（时钟使能）
2. `HAL_Delay(1)`（≥100µs）
3. `FMC_SDRAM_CMD_PALL`（全部预充电）
4. `FMC_SDRAM_CMD_AUTOREFRESH_MODE` ×8（AutoRefreshNumber=8）
5. `FMC_SDRAM_CMD_LOAD_MODE` 装模式寄存器：**突发长度 1、连续突发、CL=3、标准模式、突发写单点**
6. `HAL_SDRAM_ProgramRefreshRate`：`(64000ms/8192行)×SDRAM时钟MHz − 20`

W9825G6KH：8192 行、64ms 全行刷新周期。**注意公式里用哪个时钟**：F429 的 FMC SDRAM 时钟 SDCLK 常为 HCLK/2（90MHz@180MHz），本驱动用 `HAL_RCC_GetHCLKFreq()` 计数——2 倍过刷新，安全但浪费带宽；自己写时按 CubeMX 实际 SDCLK 分频取值。

## 6. QSPI Flash（W25Q64，bsp_w25q64.c）

**命令全参数化抽象**（QSPI 的正确封装层级——一条命令 = 指令+地址+dummy+每段线宽）：
```c
QSPI_Send_Command(instruction, address, dummyCycles,
                  instructionMode /*1_LINE..4_LINE*/,
                  addressMode, addressSize /*8/24/32_BITS*/, dataMode);
/* 内部填 QSPI_CommandTypeDef：SIOO=每命令发指令、无 AlternateBytes、无 DDR */
/* 收发前手写 DLR：hqspi.Instance->DLR = size - 1; 再 HAL_QSPI_Transmit/Receive */
```
**操作流程模板**（与 SPI 版对照）：
- 读 ID：`ManufactDeviceID_CMD(0x90)` + 地址 0 + 收 2 字节 → `(buf[0]<<8)|buf[1]`（W25Q64=0xEF17）
- 读：`Wait_Busy` → `READ_DATA_CMD(0x03)` 24bit 地址 → Receive
- 擦除：**`sector_addr × 4096`（API 收扇区号非地址！与 SPI 版直接收地址不同，混用必炸）** → WREN → Wait → `SECTOR_ERASE_CMD(0x20)` → Wait
- 页编程：WREN → `PAGE_PROGRAM_CMD(0x02)` 24bit 地址 → Transmit ≤256B → Wait
- 忙等：`while((W25QXX_ReadSR(1) & 0x01) == 0x01);`（SR1 BUSY 位）
- WREN 后调 Wait_Busy 属过度保守（WEL 即时生效），无害。

本驱动只用了单线模式（全 `QSPI_x_1_LINE`）；四线快读需 dummy 周期（Fast Read Quad 0x6B=8 周期 / 0xEB=4+6）与 QE 位（SR2 bit1）——而本库 ReadSR 有贯穿 bug（§8），启用四线前先修它。

## 7. 字模与图片资源组织

| 文件 | 内容 | 大小 |
|---|---|---|
| LCD-SPI/font.h + bear.h | ASCII asc2_1206/1608/2412/3216 四套 + 示例图片 | 72K + 590K |
| LTDC/font/font_ascii.h | ASCII 字库 | 72K |
| LTDC/font/hz16/24/32.h | GB2312 全量汉字三号 | 1.3/3.3/5.9 MB |
| LTDC/font/font_hz.h | 自选汉字精简库（开关切换） | 6K |

生成建议：ASCII 用取模工具（PCtoLCD2002，tools/ 内附）；汉字全库放外部存储按偏移读取，或按项目实际用字生成精简库；图片用 Image2Lcd 转 C 数组（RGB565 大端）。

## 8. 该库实测 bug 清单（引用驱动前逐条核对）

1. **switch 贯穿家族（3 处同款）**：`bsp_w25q64.c W25QXX_ReadSR()` case 1/2/0 全无 break 落到 default——**所有调用都读 SR1，SR2 永远读不到**（QE 位在 SR2，四线使能不可用）；`lcd_spi_drv.c lcd_draw_char()` case 12 落 16、case 24 落 32——**12/24 号字体实际用 16/32 号字库**（窗口按 12/24 算，数据按 16/32 发，显示溢出错位）；`lcd_rgb_ltdc_drv.c lcd_show_char()` case 12 同款落 16。特征：都"能编译能跑"，只有用到特定分支才坏。
2. **HTS221 温度校准值读错**（HTS221.c:73-79）：0x3C~0x3F 四字节分两次读进**同一对 buffer[0]/[1]**（先读的被后读覆盖），合成却用 buffer[3]<<8|buffer[2]（从未写入，栈垃圾）→ T0_out=T1 原始值、T1_out=垃圾，**线性插值斜率必错**。湿度路径"读完立即合成"所以幸免。
3. **lcd_fill_rect（SPI 版 USE_SPI_WRITE_BUF 分支）忽略矩形参数**：`lcd_address_set(0,0,W-1,H-1)` 写死全屏窗口——局部填充（clear_rect）会把颜色涂到全屏开头；lcd_clear 恰好全屏所以没暴露。另 remain 分支发整缓冲而非 remain_size。
4. **lcd_draw_line（SPI+LTDC 两版）x_temp/y_temp 未初始化**即累加（栈垃圾起点），Bresenham 步进随机偏移。
5. **lcd_fill（LTDC 版）off-by-one**：rect_width=x2-x1 无 +1（fill_with_buffer 有 +1），矩形右/下边少画一行列；边界检查 `y2 > LCD_HEIGHT` 漏查（查了两次 y1）。
6. **lcd_show_image 65535/65536 混用**：分块阈值 65536、发送长度 65535——绝大多数尺寸碰巧对（最后一块 remainder 补齐），整除边界多发/少发。
7. dw1000.c RSTn 重初始化依赖 main.c 已开 GPIO 时钟（validator 真 VIOLATION）；led_drv.c 时钟走 `LED_CLK()` 宏封装（validator 已降 WARNING）。
8. OLED Set_Pos `|0x01` 强制奇数列（已沉淀 hal-driver-patterns.md §6）。
9. SGP30：软复位走 I2C 通用地址 0x00（总线上有其他支持 general call 的器件会被一起复位）；未做基准校准（前 15s 返回 400ppm 固定值，手册行为，应用层要知道）。
10. HC-SR04：`while` 等 Echo 起止双死等无超时（探头缺失永久挂死，同坑 45 家族）。

## 9. validator 记录

- 23 文件：21 PASS / led_drv.c（宏时钟→新规则降 WARNING，PASS）/ dw1000.c（时钟真在 main.c，VIOLATION 属实——引用该驱动时把时钟使能收进本模块或注明依赖）。
- 修复：时钟检查新增「宏封装时钟使能」识别——形如 `[A-Z_]*CLK[A-Z_]*()` 的宏调用存在时，VIOLATION 降为 WARNING「请人工核对宏体确含 RCC 使能」（单文件分析看不见宏展开，不能当确凿证据清零也不能冤枉）。回归：bad_sample 10 VIOLATION 不变、模板 PASS 不变。

## 10. 生成检查单（显示/存储驱动）

1. 选总线先查 §1 表：屏内 GRAM（MCU 屏）走命令流；帧缓冲屏（RGB）走内存直写——两套 API 形态完全不同，别混。
2. FMC/F429 以上带 Cache：LCD/SDRAM 区域 MPU 必配 NOT-Cacheable（§3），否则刷屏丢序。
3. LTDC 帧缓冲放 SDRAM：先跑 SDRAM 六步初始化（§5），帧缓冲地址与刷新率公式按实际 SDCLK 算。
4. QSPI 命令封装到「指令/地址/dummy/线宽」层级；DLR=size-1 在每次收发前写。
5. 涉及 Flash：API 约定（扇区号 vs 字节地址）在头文件注释写死；写前擦、页编程 ≤256B、写后 Wait_Busy。
6. 引用第三方驱动：先跑 validator + §8 清单逐条核对（尤其 switch 贯穿与 buffer 复用），修完再进工程。
7. 大字模/图片资源：明确存放位置（Flash const / 外部存储），给裁剪开关。
