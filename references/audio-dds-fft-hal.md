# DDS 电子琴 + FFT 频谱仪（F103VCT6 HAL，实测工程沉淀）

> 来源：D:\stm32project\pengzhongxin（CubeMX 6.7.0 + FW_F1 1.8.4 + MDK V5.32，STM32F103VCT6 LQFP100 高密度，已编译）。
> 状态：文件级读码沉淀，未上板复测。工程为 DDS 电子琴（七音阶正弦发生）+ 音频 FFT 频谱仪（20 段柱状图）+ AD8402 数字电位器音量 + ILI9325 FSMC 屏 GUI。
> 本文是 HAL 轨「定时器触发 DAC/ADC + DMA + CMSIS-DSP」组合的第一份实测范本；SPL 四轴/平衡车见对照文档。

## 1. 架构总览：两条定时器定速的信号链

```
【发声链】 TIM2 TRGO ──触发──> DAC CH1 (PA4) ──> DMA2_CH3 (CIRCULAR) <── 波表 Sine12bit_250[250]
             ▲ 改 PSC = 变音高（EXTI 按键回调里重配）
【采集链】 TIM3 TRGO (5479Hz) ──触发──> ADC1 IN1 (PA1) ──> DMA1_CH1 (NORMAL, 1024点)
             └─> 主循环 arm_cfft_f32(1024) → arm_cmplx_mag → 20段对数频段 → FSMC LCD 柱状图
【节拍】  TIM1 1ms 中断 → count 递减（音符时值）
【人机】  K0~K9 十键 EXTI；AD8402 数字电位器（GPIO 模拟 SPI）调音量；MUTE 脚静音
```

- 发声与采集**互不依赖**：DAC 靠 DMA CIRCULAR 自转，ADC 一批采完置标志主循环算 FFT。
- 与四轴（TIM1 1ms 时间片轮询全业务）、平衡车（EXTI 驱动控制环）对照：本工程是**事件+DMA 混合**——重活全交 DMA，CPU 只做 FFT 与刷屏，主循环极轻。
- 教训级差异：四轴把控制环塞 ISR 是被 OLED 软 I2C 逼的；本工程刷屏走 FSMC（内存写），主循环可承担。

## 2. 硬件资源与引脚表（STM32F103VCT6，LQFP100，256K Flash / 48K RAM）

| 外设 | 引脚 | 配置 | 用途 |
|---|---|---|---|
| DAC1_OUT1 | PA4 | T2_TRGO 触发，OutputBuffer 使能 | 正弦波输出 |
| ADC1_IN1 | PA1 | T3_TRGO 外部触发，239.5 周期 | 音频采样 |
| DMA2_CH3 | — | 半字 CIRCULAR，M2P | DAC 波表循环 |
| DMA1_CH1 | — | 半字 NORMAL，P2M | ADC 批采样 |
| TIM1 | — | 72-1/1000-1，UP 中断 | 1ms 节拍（音符计时） |
| TIM2 | — | PSC=psc(全局变量)/Period=0 | DAC 触发源（变频=变 PSC） |
| TIM3 | — | 72/180-1，TRGO=UPDATE | ADC 采样触发 5479Hz |
| FSMC Bank1 NE1 | PD7(NE1) PD4(NOE) PD5(NWE) PD11(A16) D0~D15 | 16 位 SRAM 模式 | ILI9325 并口 LCD |
| USART1 | PA9/PA10 | 115200 | printf 调试 |
| K0~K9 | PC10/PC11/PC6~9/PD12/PD13/PB14/PB15 | EXTI 下降沿+上拉 | 音符/音量/静音 |
| SPI(模拟) | PB3=SCK PB5=SDA PA15=CS | GPIO 软件位拆 | AD8402 数字电位器 |
| MUTE | PB4 | 上拉输出，默认高 | 模拟静音 |
| LED | PC13 | 推挽输出 | 心跳 |

- F103**xC/xE**（HD）才有 DMA2 与本规模 FSMC；C8T6（MD）做不出这套（无 DMA2，DAC 仅 1MSPS 双通道受限较少但引脚不够）。选型提示：**用到 DAC+ADC+DMA 双通道并存的音频类应用，最低 HD 密度起步**。
- MspInit 全默认：NOJTAG 释放 PA15/PB3/PB4（`__HAL_AFIO_REMAP_SWJ_NOJTAG()`）——**这三个脚正是模拟 SPI 与 MUTE**，JTAG 不关则引脚冲突（经典坑，hal-basics 已有，此处是"为什么必须关"的实例）。

## 3. DDS 发声链：改 TIM2 PSC = 改音高

### 3.1 波表与频率公式

```c
const uint16_t Sine12bit_250[250] = {2048, 2098, ..., 2048};  // 12bit 中点 2048，250 点/周期
// f_out = 72MHz / (PSC+1) / 250        （TIM2 Period=0，每次 TRGO 搬一个点）
```

七音 PSC 表（实测源码常量，OCTAVE scale=1）：

| 音 | PSC | 实际频率 | 目标 |
|---|---|---|---|
| Do | 1098 | 72e6/1099/250 = 262.1 Hz | 261.6 ✓ |
| Re | 978 | 294.1 | 293.7 ✓ |
| Mi | 871 | 330.4 | 329.6 ✓ |
| Fa | 824 | 349.3 | 349.2 ✓ |
| Sol | 733 | 392.9 | 392.0 ✓ |
| La | 653 | 441.1 | 440.0 ✓ |
| Si | 582 | 494.8 | 493.9 ✓ |

- 精度分析：PSC 整数分频在低频段误差 <0.3%，音乐上足够（人耳音分阈值约 ±10 音分）。要更准可改**双寄存器**（PSC 粗调+Period 细调）或 DDS 相位累加器法（查表步进），此工程用最简单的 PSC 法。
- 波表 250 点非 2 的幂——纯查表无妨；若做 FFT 相关（如回环测量）需 256 点。

### 3.2 变频手法：EXTI 回调里重跑 Init 全家桶（反面教材+正解）

按键回调（ISR 上下文）实际代码：

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  // ...按下 K3~K9: psc = 1098/scale 等; count = 100;
  MX_TIM2_Init();                  // 完整 HAL_TIM_Base_Init（含 MspInit 判重、TIM_Base_SetConfig）
  HAL_TIM_Base_Start(&htim2);
  HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, (uint32_t*)Sine12bit_250, 250, DAC_ALIGN_12B_R);
}
```

能响，但三层问题：
1. **ISR 里跑 Init 级 API**：HAL_TIM_Base_Init 走一遍 MSP 回调判重+全寄存器配置，时长与风险（HAL 锁在 ISR 内不嵌套倒不会死锁，但任何后续改动加 osDelay/中断嵌套就炸）。
2. **DAC DMA 每键重启**：CIRCULAR DMA 本可常开，变音高只需动 TIM2——Start_DMA 反复调是浪费（且每次进入 HAL_DMA_Start 有链表操作）。
3. **tim.c 里 MX_TIM2_Init 被 #if 0 改造**：生成体注释掉、复制体读全局 `psc` 放进 USER 区（重生成安全），维护双份影子代码。

**正解三行**（保 DMA CIRCULAR 常开）：

```c
__HAL_TIM_DISABLE(&htim2);          // 或 HAL_TIM_Base_Stop
htim2.Instance->PSC = psc - 1;      // 或 __HAL_TIM_SET_PRESCALER
__HAL_TIM_ENABLE(&htim2);           // PSC 在下次更新事件装载，停-改-跑最稳
```

CubeMX 侧只在界面把 TIM2 的 Prescaler 留 0，运行时 SET_PRESCALER；不需要动生成体。

### 3.3 音符时值：count 倒计时 + 管风琴式长按

- 按下：EXTI 回调 count=100；TIM1 1ms 中断 `if(count) count--`。
- 主循环 `if(count==0){ HAL_TIM_Base_Stop(&htim2); HAL_DAC_Stop_DMA(...); }` 停音。
- **长按不断音**：主循环 keyDown() 检测按键仍按住就重置 count=100——松手后 100ms 尾音，自然。
- 结构评价：起音在 ISR（快响应）、停音在主循环（慢路径），是"急事快办"的正确分工；但 keyDown 里 `while(HAL_GPIO_ReadPin(...)==0);` **死等松手**（音量键）阻塞 FFT 刷新——见 §8。

## 4. 采集链与 FFT 频谱仪

### 4.1 采样率（注意：源码注释是错的）

```c
TIM3: Prescaler=72, Period=180-1   // CubeMX 里写的 72 = PSC 寄存器值，实际 73 分频！
f_sample = 72e6 / (72+1) / 180 = 5479.5 Hz
```

- 源码注释「72000000/72/100=10khz」两处失实（73 分频算成 72、Period 180 写成 100），下游 FFT 频段标签按 10kHz 标注，**整条频段标尺系统性偏移 1.87 倍**（注释 bin2=19.5Hz，实际 2×5.35=10.7Hz）。
- 沉淀：**CubeMX Prescaler 域是寄存器值不是分频比**（分频=PSC+1）；注释里的任何频率必须用寄存器值重算（坑 52）。
- 附带配置假象：ADC 的 ContinuousConvMode=ENABLE 与 ExternalTrigConv=T3_TRGO 同时存在——F1 外部触发模式下 Continuous 无效，起节拍作用的是 TIM3（坑 54）。

### 4.2 CMSIS-DSP 集成（1024 点实数频谱）

```c
// 采样：HAL_ADC_Start_DMA(&hadc1,(uint32_t*)adcBuff,1024) → DMA TC 中断置 AdcConvEnd
// 主循环：
for(i=0;i<1024;i++){ fftBuf[2*i]=adcBuff[i]*3.3f/4096; fftBuf[2*i+1]=0; }  // 实→复
arm_cfft_f32(&arm_cfft_sR_f32_len1024, fftBuf, 0, 1);   // 位反转+归一化由参数 1,1 承担
arm_cmplx_mag_f32(fftBuf, fftBuf, 1024);                 // 就地取模
```

- **arm_const_structs.h** 的 `arm_cfft_sR_f32_len1024` 是预生成 twiddle 表实例，工程属性里需勾 CMSIS-DSP 源码组（本工程全套 8 组都编了——实际只用到 CFFT 与 magnitude，可在裁剪时只留 CommonTables+TransformFunctions+ComplexMathFunctions 三组）。
- bin 宽 = Fs/1024 = 5.35Hz；显示用 20 段**对数分组**（bin 2/4/7/11/16/22/28-30/.../481-483，每段 1~3 bin 求和/取峰），覆盖 ~10Hz~2.6kHz。
- 输入范围：3.3V 满量程、无偏置电路描述——直接采 PA1，疑似接麦克风模块输出或 DAC 回环；若接交流信号需偏置到 Vref/2，否则负半周削顶（本工程硬件未知，生成时提醒用户）。

### 4.3 频谱柱 UI：峰值保持 + 缓降

```c
topBuf[x] = (height > topBuf[x]) ? height : topBuf[x]-1;   // 每帧最多降 1 格
```

- 20 段柱、每段 15 行可视；显示函数写 20 行、段间距 15 行——**后段覆盖前段 5 行**（依赖 GRAM 地址自增越过 region 后被下段重写，ILI9325 行为，见 §8 风险）。
- topBuf[20] 初值全 0；峰值保持是频谱仪经典 UI，缓降一格让眼睛跟得上。

## 5. AD8402 数字电位器（10kΩ 双通道）

```c
// 写时序（GPIO 模拟 SPI，位拆 MSB-first）：
// CS 低 → 2bit 通道地址（01=W0/10=W1/11=双通道） → 8bit 数据（0~255 线性） → CS 拉高锁存
setVolume(v): CS低; 移出0b01; 移出v; CS高;  CS低; 移出0b10; 移出v; CS高;   // 双通道同写
```

- AD8402 是**电位器抽头**（三端），接成分压器串在 DAC 输出与功放之间=电子音量；写 255≈10kΩ（最大衰减）。
- 与 PWM+RC 滤波调音量对比：数字电位器无量化噪声、通道间可同步，是音频路径正解。
- 本工程音量 10~240 步进 10（K1/K2 键），MUTE 脚硬静音兜底（上电默认高=静音，**安全取向正确**）。

## 6. ILI9325 FSMC 驱动要点（增量，完整总线对照见 display-storage-hal.md；地址换算原理与 A10→bit11 实例见 fsmc-lcd-ili9341.md §2/坑 4）

| 知识点 | 内容 |
|---|---|
| 寄存器地址 | `CMD=0x60000000`，`DATA=0x60020000`。**A16 接 RS，但 0x20000 是 HADDR bit17**——16 位宽存储器 FSMC 内部把字节地址右移 1 位映射到 FMC_Ax，所以 RS 线 A16 的数据地址偏移=1<<(16+1)=0x20000。算成 0x10000 是新手首错（点不亮/命令数据错乱） |
| 0x22 读写复用 | MEMORY_WRITE 与 MEMORY_READ 同为 0x22（ILI9325 GRAM 口），靠 FSMC NOE/NWE 读写时序区分 |
| 读 GRAM | 手册明确 address counter 读时不自增 → **每像素 SetPos+MEMORY_READ+dummy read+真读**（源码留了原文注释）；批量读屏必须逐像素寻址，别想 DMA |
| 初始化 | 0xE5 SRAM 时序→0x01/0x02 驱动→0x03=0x1030（横屏+I/D=11+BGR）→电源四级上电（0x10/0x11/0x12/0x13 + 20ms）→0x29/0x2B→gamma 0x30~0x3D→0x07=0x0133 开显示；**无复位脚操作**（依赖上电复位） |
| SetPixel | 每点都 SetRegion(1×1)+CMD+DATA——慢但简单；批量画改 SetRegion 一次+连续 DATA |

- 显示方向 0x03=0x1030：横向 240×320，原点左上（I/D=11：X 左→右、Y 上→下，AM=0）。
- 489KB images.c 图片资产直接 const 数组编进 Flash（256K Flash 装 489K 图片？——**装不下**，见 §8 疑点）。

## 7. CubeMX 工程结构备注

- **tim.c 的 #if 0 改造**：MX_TIM2_Init 原生成体整体注释，复制体（Period=psc 全局变量）放 USER CODE TIM2_Init 0/2 区内——重生成安全（USER 区保留），代价是"生成体-影子体"双份漂移风险；正解见 §3.2。
- NVIC PriorityGroup=4（全抢占），TIM1_UP(0,0) 最高、EXTI/DMA 次之——无嵌套设计，分组差异不敏感（对照平衡车坑 49）。
- it.c USER 区：DMA1 TC 置 AdcConvEnd=1；TIM1_UP 调 myTim1CallBack()（main.c）再走 HAL_TIM_IRQHandler——**HAL 处理器与用户钩子并存的标准写法**。
- usart.c USER 区 fputc/fgetc（阻塞 HAL_UART_Transmit/Receive 超时 0xffff）——Keil MicroLIB 可用。
- 工程树有 lcd.o/lcd.crf 遗迹但无 lcd.c 源——**删除源文件未清构建产物**，判读历史时注意（无害）。

## 8. Bug 与现场教训清单

1. **FFT 频段标尺错 1.87 倍**：TIM3 注释「/72/100=10kHz」实为 /73/180=5.48kHz；频段标签全按 10kHz 标。柱状图当 UI 无碍，当仪器读数就是错的（坑 52 家族）。
2. **scale（八度切换）死参数**：定义 `uint8_t scale=1`、psc=1098/scale 除它，但**全工程无任何按键改 scale**——八度切换功能没接线。调参旋钮死参数家族第三例（平衡车坑 51）。
3. **keyDown() `while(按住);` 死等松手**（音量键）：阻塞主循环，FFT 冻结到松手；应改为状态机或计数去抖。
4. **display() 写入越 region**：SetRegion 高度 11 行、实写 20 行，靠 GRAM 自增+下段覆盖"凑合"；不可移植（换控制器/换扫描方向即花屏），且段重叠区每帧闪两次。
5. **EXTI 回调重配全家桶**：§3.2 反面教材（ISR 时长+API 级别误用）。
6. **去抖用 HAL_Delay(10) 轮询**：10 键全扫一圈最多 100ms 潜伏；按键已有 EXTI 边沿，去抖应挂 1ms 节拍计数。
7. **images.c 489KB 与 256K Flash 矛盾**：工程能编译过说明实际链接时可能开了散排/或图片数组大部分未引用被裁剪（--remove unused）——生成同类工程时图片资产必须核对 Flash 预算，超限先问压缩/裁剪策略。
8. **AdcConvEnd 由 DMA TC 中断置位、主循环清零**：单标志无 volatile 却"能跑"（M3 单字节原子+轮询窗口大），validator 报的 psc/scale 才是必须修的（ISR 写、主循环读、多字节 uint16_t）。

## 9. validator 记录

- `--density hd` **首次真实工程使用**：F103xE 的 `DMA2_Channel3_IRQHandler` 在 MD 向量表中不存在，默认密度报"未知向量"误报；--density hd 后消失。多密度合法向量：**跑 HD/XL 工程记得带密度参数**。
- 结果：11 文件 2 VIOLATION 0 WARNING——main.c 的 `psc`/`scale`（L102/L107，EXTI 回调访问非 volatile 全局）**全部真实**（ISR 写 psc、主循环也写读；scale 虽是死参数但 ISR 读了）。
- 其余 10 文件（生成体+驱动）全 PASS 零误报。

## 10. 生成检查单（同类"定时器触发 DAC/ADC+DMA+DSP"工程）

1. DAC 波表 DMA 必须 **CIRCULAR**；ADC 批采样用 **NORMAL**（采完即停，主循环消费）——方向/模式对调是首错。
2. 触发源成对检查：DAC_Trigger=T2_TRGO ⇔ TIM2 TRGO=UPDATE；ADC ExternalTrig=T3_TRGO ⇔ TIM3 TRGO=UPDATE。漏 TRGO 配置=外设等一个永不到来的触发。
3. 采样率用寄存器值验算：f = 72MHz/(PSC+1)/(ARR+1)，**注释数字必须与此式一致**。
4. 变频/变采样率：停-改寄存器-跑 三行法，不重跑 MX_xxx_Init；DMA CIRCULAR 通道全程只 Start 一次。
5. EXTI 里只置标志+轻量参数赋值；Init 级 HAL 调用与长循环不许进 ISR。
6. FFT 输入要偏置：交流信号先偏到 Vref/2，否则负半周削顶；bin 宽=Fs/N 写进注释。
7. 跨 ISR/主循环变量一律 volatile（uint16_t 也要——psc 正是 uint16_t）。
8. FSMC 16 位 LCD 的 RS 地址 = base | (1 << (A+1))，写宏前先验算一遍。
9. 图片资产 const 数组 × Flash 预算，超限先裁剪再链接。
10. AD8402 类写保护：CS 平时高，写完立刻拉高锁存；上电默认音量最小/静音（本工程 MUTE 上电高=静音 ✓）。
