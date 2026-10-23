# FSMC 驱动 TFT-LCD（ILI9341/9328，SPL）

> 来源：实验九工程（Nemo M3S 板，STM32F103 大容量 + 16 位并口 ILI9341 240×320）实测沉淀。
> FSMC 仅 **HD/XL 大容量** F1 才有（C8T6 没有）；小容量驱动 TFT 用 GPIO 模拟并口或 SPI 屏。

## 1. 地址译码原理（必须先懂，换板全靠它）

LCD 挂在 FSMC NOR/SRAM Bank 上，**命令/数据用一根地址线（RS）区分**：

- NE4 片选（PG12）→ Bank4 基址 `0x6C000000`；NE1(PD7)/NE2(PG9)/NE3(PG10) 对应 `0x60000000/0x64000000/0x68000000`
- RS 接 **A10** → 16 位数据宽度下 HADDR 右移一位对齐，地址 **bit11** 对应 A10
- 于是两个地址就是全部接口：

```c
#define LCD_BASE  ((uint32_t)0x6C000000)
#define LCD_CMD   (*((volatile uint16_t *)(LCD_BASE)))                /* RS=0 写命令 */
#define LCD_DATA  (*((volatile uint16_t *)(LCD_BASE | (1u << 11))))   /* RS=1 读写数据 */
```

写 `LCD_CMD = reg` 就是发命令，写 `LCD_DATA = x` 就是写数据——一次总线写，无软件时序。

## 2. 引脚清单（FSMC 复用推挽 50MHz）

| 信号 | 引脚 | 信号 | 引脚 |
|---|---|---|---|
| D0,D1,D8,D9,D10,D14,D15 | PD14,15,0,1,8,9,10 | NOE(读) | PD4 |
| D2~D7,D12,D13 | PE7~PE15 中对应脚（本板 PE7~15 全配） | NWE(写) | PD5 |
| NE1~NE4 片选 | PD7, PG9, PG10, PG12 | RS(A10) | PG0 |
| 背光 BL | PB0（独立推挽 GPIO，非 FSMC） | RST | 硬件复位电路 |

时钟：`RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC)` + 所涉 GPIO 全部 `RCC_APB2Periph` + **AFIO**。

## 3. FSMC 初始化（SPL 模板，保守慢速时序）

```c
FSMC_NORSRAMTimingInitTypeDef t;
t.FSMC_AddressSetupTime = 0x0F;      /* 建立时间 15 HCLK ≈ 208ns：先慢保命，稳定后再收 */
t.FSMC_DataSetupTime    = 0x0F;      /* 数据保持 15 HCLK */
t.FSMC_AccessMode       = FSMC_AccessMode_B;

FSMC_NORSRAMInitTypeDef f;
f.FSMC_MemoryType      = FSMC_MemoryType_NOR;      /* LCD 按 NOR 时序接 */
f.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;
f.FSMC_WriteOperation  = FSMC_WriteOperation_Enable;
f.FSMC_ExtendedMode    = FSMC_ExtendedMode_Disable; /* 读/写共用同一时序 */
/* 其余 Burst/WaitSignal/Wrap 全 Disable */
FSMC_NORSRAMInit(&f);
FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM4, ENABLE);
```

**All-NE 技巧（多排座/兼容板）**：不知道屏座插哪个 Bank 时，把 4 个 Bank 用同一时序全部 `Init+ENABLE`，再提供 `LCD_ReadID_AtAddress(base,…)` 按基址探测——实测工程即此做法。单板确定 NE 后只配一个 Bank，省 GPIO。

## 4. 面板初始化流程

1. 上电 → 背光亮 → `Delay_ms(200)` → 软复位 `0x01` → `Delay_ms(150)`（>手册 120ms 上限）
2. 读 ID 识别型号：写 `0xD3` → **空读一次丢掉**（dummy）→ 再读两次得 ID（9341=0x9341）
3. 按型号走各自寄存器初始化序列（ILI9341 与 ILI9328 序列完全不同，工厂代码里通常两套 `xx_Init()` + 一层 `LCD_Init()` 分发）
4. GRAM 写入口：命令 `0x22`（9328/9341 相同），之后连写 `LCD_DATA` 即像素流

## 5. 绘图 API 与性能约定（实测工程惯例）

- `SetWindow(x0,y0,x1,y1)`：列地址 0x2A / 行地址 0x2B 设窗口 → `0x22` 连续写像素。**Fill/Clear 全靠窗口批量写**，不逐点画
- 画点 = SetWindow 1×1 + 写 1 像素（慢，只用于网格/描点）；连线用 Bresenham 逐点
- 波形刷新的正确分工（实测）：`LCD_Fill()` 黑底擦旧 → 画网格（隔 4 点）→ `LCD_DrawLine()` 连新波形 → 信息栏局部 `Fill+ShowString`；**每 10ms 主循环判 `g_rw` 标志才刷**，不整屏重画
- 字符/数字/浮点/printf 封装（`LCD_ShowString/ShowNum/ShowFloat/Print`）与 OLED 江协 API 同风格，字模 `const` 放 Flash

## 6. RGB565 与 R/B 硬件交换

16 位色 `RRRRRGGG GGGBBBBB`，但本板排线把 R/B 接反——实测颜色表是**交换后的值**：`LCD_RED=0x001F`、`LCD_BLUE=0xF800`、`LCD_CYAN=0xFFE0`。
移植检查：刷 `0xF800` 看显示红还是蓝，反了就整体交换 R/B 位域（改颜色宏，不改驱动）。

## 7. 坑（实测/审查）

1. **读 ID 必须先空读一次**：首读是上条命令残留，直接取值必错型号
2. **时序宁慢勿快**：0x0F/0x0F 稳定后逐级下调试极限；一上来快时序=花屏/白屏且难排查
3. **FSMC 时钟是 AHB 72MHz**，`AddressSetupTime` 单位是 HCLK 不是 ns——按 13.9ns/格换算
4. **RS 接哪根地址线是板级设计**：接 A10 就用 bit11（16 位宽度右移），接 A0 则 bit1——照抄别板代码前先对原理图
5. **FSMC 与按键共总线**：并行总线全速翻转易串扰相邻走线，按键判输入用 `GPIO_ReadInputDataBit` 前确认脚不在 FSMC 复用清单里（实测工程把 S1~S5 放 PC0~PC4 就是为此）
6. 初始化序列来自面板厂/卖家例程，**逐条对照手册审查**（第三方驱动必有 bug，见坑 30）

## 8. 与本技能其他文档的衔接

- 显存/字模/API 风格对照：`oled-ssd1306.md`（同为"设置窗口+连续写"模型，OLED 是页寻址、TFT 是线性窗口）
- 波形发生器整体信号链架构：`peripheral-recipes.md` §13
