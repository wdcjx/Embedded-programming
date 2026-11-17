# CHANGELOG — embed 技能

## v0.12.0（2026-11-17）

- **validator 1.1.0**：规则 ID R1~R15/R99 集中映射；`--format json` 输出（CI/工具可解析）；新规则 R13 GPIO 端口存在性（按芯片密度）；修复 neutralize 抹字符串导致 RTOS 检测失明的真实缺陷。
- **规则级单测**：`tests/rule_snippets/` 15 文件 + `run_rule_tests.py`（断言规则 ID 而非计数，防规则静默失效），首跑即抓出 validator 真实缺陷。
- **真实编译 CI**（compile workflow）：arm-none-eabi-gcc 真编模板与示例（SPL V3.6.0 锁 Abysmo 镜像 ff7527a、FreeRTOS-Kernel 10.5.1 锁 d7b40db，全部 commit SHA 钉死）；模板第一次被真编译器验证。
- **evals 评测集**：`tests/evals/TASKS.md` 10 个标准任务书+评分。
- SKILL.md frontmatter `version:`；actions 三 workflow 全钉 SHA；删除 Untitled.py（用户指示）。
- 技能自更新指令（v0.11.4）：`npx skills update embed`。
- 外部审查修复（v0.11.6）：模板初始化竞态 NULL 守卫、C90 声明位置；坑 92。

面向技能使用者的版本摘要（技能装载侧读不到 git tag，frontmatter `version:` 与本文件对齐）。
完整演进记录见 [ROADMAP.md](ROADMAP.md)。

## 0.11.7（2026-11-17）

外部审查清单落地（工程化补强）：

- **真实编译 CI**：`.github/workflows/build.yml` 用 gcc-arm-none-eabi 编译 SPL 双轨模板+示例（依赖按 commit SHA 锁死：SPL V3.6.0 快照 + FreeRTOS V10.5.1）；HAL 轨暂由 validator 覆盖（诚实边界）。
- **validator 1.1.0**：规则 ID（R1~R15/R99）稳定编号；`--format json` 机器读输出（带 validator 版本）；新规则 R13 引脚端口存在性（C8T6 无 GPIOD/E，链接期 undefined symbol）；修复 RTOS/HAL 检测在 neutralize 后对 include 线索失配的真实缺陷（规则级单测 R4 抓出）。
- **规则级单测**：`tests/rule_snippets/` 15 条规则每条一个最小触发文件，`tests/run_rule_tests.py` 断言规则 ID 而非计数——防"规则静默失效但基线全绿"。
- **evals 评测集**：`tests/evals/TASKS.md` 10 个标准任务书+期望，度量 SKILL.md 教学效果。
- **供应链**：全部 workflow actions 按 commit SHA 钉住；SKILL.md frontmatter 加 `version:`；新建本 CHANGELOG。
- 删除 Untitled.py（用户指示变更，v0.1 的"保留"指示已被取代）。

## 0.11.6（2026-11-17）

- 修复外部审查发现的模板真实 bug：`main_rtos_spl.c` 初始化竞态（EXTI0 使能早于任务句柄赋值，ISR 加 NULL 守卫）；C90 兼容（3 处声明挪块首）；删除 Windows 保留名残留文件 CON；坑 92。

## 0.11.5（2026-11-15）

- 新建 `references/hub75d-wifi-clock-hal.md`（HUB75D 点阵 WiFi 时钟：DMA-ODR 显示驱动/ESP8266 AT 表驱动校时/双分区 OTA）；坑 89~91。

## 0.11.4（2026-11-11）

- SKILL.md 新增自更新指令（`npx skills update embed` / git pull 路径）。

## 0.11.3（2026-11-11）

- mcu_quick_start 全库沉淀（stm32-stdperiph-api §10 九型号参数表+GD32 兼容链、rocketpi-rtos-ports §5 RT-Thread Nano 手工集成）；KW407A 教程增量；坑 87/88。

## 0.11.2（2026-11-10）

- 新建 `references/portable-lib-design.md`（MultiButton 单文件库设计模式）；坑 86。

## 0.11.1（2026-11-07）

- validator 新增 EMBED MANIFEST 聚合检查（跨文件引脚冲突/向量重复——单文件分析盲区的工程级补法，XRobot manifest 思路）。

## 0.11.0（2026-11-06）— 第三个公开版本

- 29 篇文档 / 85 坑；新增 BMS（BQ769x0）、达妙电机 FOC、多协议总线分析仪、门禁、RFID 门禁、HUB75D 等领域；CI 自动回归上线；英文速览段；SKILL.md 瘦身。

## 0.9.1（2026-10-30）— 首个公开版本

- 18 篇文档 / 48 坑 / 四轨模板 / validator / npx 安装。

（更早版本见 ROADMAP.md）
