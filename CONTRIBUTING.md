# 贡献指南

## 反馈问题

1. 先查 [references/known-pitfalls.md](references/known-pitfalls.md)（73 条实测坑）和
   [references/symptom-quickref.md](references/symptom-quickref.md)（症状反查表）——多数现场问题已有归因；
2. 没命中再开 [Issue](https://github.com/wdcjx/Embedded-programming/issues)，请附：芯片型号、
   库轨（SPL/HAL）、是否 RTOS、现象描述、可复现的最小代码片段。

## 知识沉淀贡献（本仓库最主要的贡献类型）

- **新坑**：按 known-pitfalls.md 现有格式（症状/原因/修法 + 工程出处 + 实测标注）追加，
  编号顺延；
- **新 reference 文档**：优先来自**真实上板工程**，文首注明来源与实测状态（上板/编译通过/未编译），
  生成检查单放文末；SKILL.md 资源表、README 文件树同步加行；
- **validator 规则增强**：必须附回归——`tests/bad_sample*.c` 现有违规数不变、
  `templates/*.c` 保持 PASS、新规则在真实工程上的误报说明。

## 代码/文档修改流程

fork → 分支 → 修改 → 自测（改动涉及生成代码时跑
`python scripts/validate_generated.py <文件> --density md`）→ PR，说明来源工程与实测状态。

## 约定

- 文件一律 UTF-8；中文文档；引用源码带相对路径与行号；
- 沉淀开源工程时以代码为准（注释可能漂移，见坑 46）；
- 贡献内容按 MIT 许可随仓库发布。
