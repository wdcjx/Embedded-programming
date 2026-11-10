# 可移植单文件库设计范式 — MultiButton 1.1.1 全解

> 来源：0x1abin/MultiButton（Copyright 2016-2026 Zibin Zheng，MIT 类开源，单 .c/.h 共 437 行）。
> 用户工程实测视角的按键写法见 peripheral-recipes.md §按键与本文末尾对照表。
> validator：multi_button.c 0 VIOLATION 0 WARNING（技能库首个零警告文件——纯 C 库不涉 STM32 特定规则，纪律全绿）。

## 1. 库定位与一行集成

单文件、零依赖（stdint.h/string.h）、非阻塞（5ms 节拍轮询）、事件驱动。任何 MCU 任何 RTOS 可用——硬件全部通过一个函数指针注入：

```c
/* 板级唯一适配点：读按键电平 */
uint8_t read_key1(uint8_t btn_id) { return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0); }

Button btn1;
button_init(&btn1, read_key1, 0, 0);            /* 句柄、读脚函数、按下电平、ID */
button_attach(&btn1, SINGLE_CLICK, btn1_click_cb, NULL);  /* 事件→回调+user_data */
button_start(&btn1);                            /* 挂入内部链表 */
/* SysTick/TIM 每 5ms 调一次 button_ticks()——库的心跳，从不阻塞 */
```

SPL 轨集成要点：TICKS_INTERVAL=5 必须与实际节拍一致（SHORT/LONG_TICKS 都是它的倍数换算）；多按键各自 button_init+attach+start，共用同一个 button_ticks()。

## 2. 事件模型：7 事件 + 每事件独立回调 + user_data

```
PRESS_DOWN / PRESS_UP / PRESS_REPEAT / SINGLE_CLICK / DOUBLE_CLICK
LONG_PRESS_START / LONG_PRESS_HOLD
```

- 回调表是结构体成员数组 `BtnCallback cb[BTN_EVENT_COUNT]`：每事件挂一个，不是 switch-case 中央分发——加事件不动库代码。
- 回调签名带 `void* user_data`（button_init 时 memset 清零、button_attach 时存入）：上下文随句柄走，库内零全局变量（唯一 static 是链表头）。
- 轮询模式也支持：不 attach，主循环 button_get_event() 查询（poll_example.c）。
- EVENT_CB(ev) 宏统一判空调用——回调可空挂。

**何时发什么**：PRESS_DOWN 在按下沿立即发（IDLE 与 RELEASE/REPEAT 态都发）；SINGLE/DOUBLE_CLICK 在松手后 SHORT_TICKS(300ms) 静默窗口到点才发（判定需要"等不再按"）——**即时事件沿上发、归纳事件超时发**，这个二分是按键事件学的核心语义。

## 3. 状态机全解（5 态）

```
IDLE ──按下──> PRESS ──松手──> RELEASE ──再按──> REPEAT ──松手(短)──> RELEASE(续)
                 │                │                 │
                 │>LONG_TICKS     │>SHORT_TICKS静默  │>SHORT_TICKS按住
                 v                v                  v
             LONG_HOLD      repeat==1→SINGLE     降级回 PRESS
             (HOLD周期发)    repeat==2→DOUBLE     (重新计长按)
                 │                └─>IDLE
                 └─松手──> PRESS_UP → IDLE
```

| 态 | 进入条件 | 在态期间 | 退出 |
|---|---|---|---|
| IDLE | 复位/归纳完成 | 等按下沿 | 按下→PRESS（发 DOWN，ticks=0，repeat=1） |
| PRESS | 按下沿 | 计时 | 松手→RELEASE（发 UP，ticks=0）；>LONG_TICKS→LONG_HOLD（发 START） |
| RELEASE | 松手 | 静默窗口计时 | 再按→REPEAT（发 DOWN+REPEAT，repeat++）；>SHORT_TICKS→按 repeat 归纳 CLICK→IDLE |
| REPEAT | 连击第 n 次按下 | 计时 | 松手且按住<SHORT→RELEASE 续等；按住>SHORT→**降级回 PRESS 重计长按**（ticks=0, repeat=0） |
| LONG_HOLD | 长按到点 | 每拍发 HOLD | 松手→PRESS_UP→IDLE |

SHORT_TICKS 与 LONG_TICKS 语义不同：前者是**双击等待窗口**（RELEASE 态），后者是**长按阈值**（PRESS 态）。REPEAT 态的 SHORT_TICKS 是第三语义：按住太久不再算连击。

## 4. 状态压缩：位域 + 编译期契约

```c
uint8_t repeat : 4;        /* 0-15 连击计数 */
uint8_t event  : 4;        /* 事件枚举 0-15 */
uint8_t state  : 3;        /* 5 态 0-7 */
uint8_t debounce_cnt : 3;  /* 去抖计数 0-7 */
uint8_t active_level : 1;
uint8_t button_level : 1;
```

6 个字段 16 位（两个字节装完），单按键句柄含链表指针共 12 字节。位域上限用**编译期契约**钉死：

```c
#if DEBOUNCE_TICKS > 7
  #error "DEBOUNCE_TICKS exceeds 3-bit field maximum (7)"
#endif
```

配置改出合法域直接编译失败，而不是运行时悄悄回绕——生成配置类代码时凡有"值域受字段位宽限制"都应带 #error 契约。

## 5. 去抖：计数滤波而非延时采样

读到电平 ≠ 当前认定电平时 debounce_cnt++，连续 DEBOUNCE_TICKS(3) 拍（15ms）一致才翻转认定值；任何一拍回弹清零。状态机永远消费"认定电平"而非原始电平。对比延时采样式（delay 10ms 再读）：本式零阻塞、对慢回弹和快回弹都鲁棒。**ISR/轮询两用**——去抖活在节拍域里，不活在阻塞延时里。

## 6. 防御性细节（可直接抄的规矩）

- **ticks 饱和递增**：`if (ticks < UINT16_MAX) ticks++;`——永远不回绕，长按一天也不会翻转成 0 误发事件。
- **参数判空全 API 覆盖**：button_init/attach/detach/reset 入口 `if (!handle...) return`；button_start 返回 0/-1(已在链表)/-2(非法句柄) 三态。
- **default 态兜底**：状态机 default 分支回 IDLE——RAM 位翻转等异常态自愈。
- button_init 里 `button_level = !active_level` 初值（认定电平=松开），首拍不会误报按下沿。
- button_reset() 只清运行时字段不动回调表——重置≠重配。

## 7. 可选线程安全：宏注入 + 锁外回调

```c
#define MULTIBUTTON_THREAD_SAFE
#define MULTIBUTTON_LOCK()   osMutexAcquire(btn_mutex, osWaitForever)
#define MULTIBUTTON_UNLOCK() osMutexRelease(btn_mutex)
#include "multi_button.h"    /* 裸机不定义 THREAD_SAFE 则锁为空宏 */
```

链表增删改（button_start/stop/ticks 遍历取 next）持锁；**所有回调在锁外执行**——回调里可以安全地再调 button_start/button_stop（增删自己/别人），不会自锁死。规矩：**持锁区间只做数据结构操作，业务回调一律锁外**；这样普通非递归互斥量就够。button_ticks() 遍历采用"每步取 next 快照"式细粒度持锁，允许遍历中其他任务改链表。

## 8. 链表管理多实例：二级指针删除

```c
Button** curr;
for (curr = &head_handle; *curr; ) {
    if (*curr == handle) { *curr = entry->next; entry->next = NULL; return; }
    else curr = &entry->next;
}
```

教科书式 `Button**`：删除时直接改"指向它的指针"（可能是 head 也可能是前驱的 next），免掉单独处理头节点/记前驱两个分支。生成任何"管理 N 个实例"的库代码（按键/传感器/通道）都应掌握此式。

## 9. 单元测试手法：mock 注入 + 节拍快进 + 事件日志

tests/test_button.c（522 行 10 用例，零依赖）三板斧：

1. **mock 读脚函数**替 HAL：`mock_gpio_value` 单变量，测试直接赋值模拟电平；
2. **tick_n(n) 快进**：循环调 button_ticks() n 次——真实时间概念被压成整数拍数，用例可精确卡 SHORT_TICKS±1 边界；
3. **事件日志数组断言**：每个事件挂一个只做 `event_log[count++]=ev` 的回调，断言 has_event/count_event。

同族先例：BMS 工程的 fake_i2c/can/rs485 注入 + fail-fast runner。按键状态机测试的增量手法是"**电平序列 + 拍数 = 完整激励**"——任何节拍驱动的状态机（按键/协议解析/充电状态机）都可套此式。

## 10. 真 bug 存档（1.1.0 修复，坑 86 实例）

1.0 版 REPEAT 态降级回 PRESS 时**不重置 ticks 与 repeat**：连击几次后按住，残留的累计拍数直接越过 LONG_TICKS → 立即误发 LONG_PRESS_START。1.1.0 修法就是两行：转移时 `ticks=0; repeat=0;`。教训已提炼为坑 86（状态机转移的携带状态清单）。

## 11. 库式 vs 工程式按键对照（选型）

| | 工程式（recipes §按键、平衡车矩阵键盘） | 库式（MultiButton） |
|---|---|---|
| 代码量 | 50~100 行内嵌 Hardware/ | +437 行库文件 |
| 事件 | 单击/长按按需手写 | 7 事件全，含双击/连击/HOLD |
| 移植 | 换板重写 | 改一个读脚函数 |
| 测试 | 上板验证 | mock 注入单测 |
| 适用 | ≤3 键、事件简单、代码量敏感 | 多键、双击/连击需求、多项目复用 |

生成建议：用户要双击/连击/HOLD 连发或按键 ≥4 个 → 直接推荐集成 MultiButton（给本文 §1 集成三行+读脚函数）；只要单击+长按 → 工程式配方更轻。

## 12. validator 记录

multi_button.c：0 VIOLATION 0 WARNING（2026-11-10，validate_generated.py v0.11.1）。零警告原因：无 ISR 定义、无硬件等待循环、无全局共享变量（回调表在句柄内）。第三方纯逻辑库接入本检查器的典型基线。

## 13. 生成检查单（写可移植单文件库时）

- [ ] 硬件访问全部经函数指针/ops 表注入，库内零寄存器名
- [ ] 配置宏受位宽/数组上限约束处带 #error 编译期契约
- [ ] 计数器饱和递增（或宽度足以覆盖最长场景并注明）
- [ ] 回调判空 + user_data 上下文；锁保护下回调在锁外执行
- [ ] 链表删除用二级指针式；遍历用快照式持锁（RTOS 场景）
- [ ] 提供 mock 注入点与节拍快进测试骨架（参考 §9 三板斧）
- [ ] 状态机每条转移旁注"清什么/留什么"（坑 86 规矩）
