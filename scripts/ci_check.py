#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CI 回归：validator 判定基线 + 仓库一致性检查。

本地可跑：python scripts/ci_check.py（在仓库根目录执行）。
CI 中由 .github/workflows/ci.yml 调用，exit 0 = 全部通过。

检查项：
  1. validator 六样本基线（三 bad_sample 必须 FAIL 且违规/警告数精确匹配，
     三 templates 必须 PASS 且违规=0）——防规则漂移（误报/漏报都会被抓住）
  2. references/ 目录与 SKILL.md 资源表双向一致（表里有的文件必须存在；
     目录里的文档必须都在表中）
  3. known-pitfalls.md 坑编号连续 1..N（无重号/跳号）
  4. README.md 中坑计数声明 == known-pitfalls.md 实际条数（防忘更新）
  5. README.md 文件树覆盖 references/ 全部文档（防新增文档忘加文件树行）
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAL = os.path.join(ROOT, 'scripts', 'validate_generated.py')

# (相对路径, 预期判定, 预期 VIOLATION, 预期 WARNING)
BASELINE = [
    ('tests/bad_sample.c',       'FAIL', 10, 1),
    ('tests/bad_sample_rtos.c',  'FAIL', 4, 1),
    ('tests/bad_sample_hal.c',   'FAIL', 3, 1),
    ('templates/main.c',         'PASS', 0, 1),
    ('templates/main_rtos_spl.c','PASS', 0, 1),
    ('templates/main_hal.c',     'PASS', 0, 0),
]

failures = []


def check(name, ok, detail=''):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ''))
    if not ok:
        failures.append(name)


# ---------- 1. validator 基线 ----------
for rel, verdict, exp_v, exp_w in BASELINE:
    path = os.path.join(ROOT, rel)
    if not os.path.isfile(path):
        check(f"validator: {rel} 存在", False, '文件缺失')
        continue
    r = subprocess.run([sys.executable, VAL, path],
                       capture_output=True, text=True,
                       encoding='utf-8', errors='replace')
    m = re.search(r'违规\(VIOLATION\):\s*(\d+)\s+警告\(WARNING\):\s*(\d+)', r.stdout)
    v = re.search(r'结果:\s*(PASS|FAIL)', r.stdout)
    got_verdict = v.group(1) if v else '?'
    got_v = int(m.group(1)) if m else -1
    got_w = int(m.group(2)) if m else -1
    ok = (got_verdict == verdict and got_v == exp_v and got_w == exp_w)
    check(f"validator: {rel} -> {verdict} V={exp_v} W={exp_w}",
          ok, f"实际 {got_verdict} V={got_v} W={got_w}" if not ok else '')

# ---------- 2. references <-> SKILL.md 资源表 ----------
skill_path = os.path.join(ROOT, 'SKILL.md')
skill = open(skill_path, encoding='utf-8').read()
listed = set(re.findall(r'`(references/[A-Za-z0-9._-]+\.md)`', skill))
actual = {f'references/{f}' for f in os.listdir(os.path.join(ROOT, 'references'))
          if f.endswith('.md')}
missing_files = sorted(listed - actual)
unlisted = sorted(actual - listed)
check('SKILL.md 资源表 -> 文件存在', not missing_files,
      f'表中有但文件缺失: {missing_files}' if missing_files else '')
check('references/ 文件全部登记在 SKILL.md', not unlisted,
      f'未登记: {unlisted}' if unlisted else '')

# ---------- 3. known-pitfalls 编号连续 ----------
kp_path = os.path.join(ROOT, 'references', 'known-pitfalls.md')
kp = open(kp_path, encoding='utf-8').read()
nums = [int(n) for n in re.findall(r'^(\d+)\. \*\*', kp, re.M)]
expected_seq = list(range(1, len(nums) + 1))
check(f'known-pitfalls 编号连续 1..{len(nums)}', nums == expected_seq,
      f'实际序列异常（重号/跳号）: {nums}' if nums != expected_seq else '')

# ---------- 4. README 坑计数一致 ----------
readme = open(os.path.join(ROOT, 'README.md'), encoding='utf-8').read()
declared = set(int(n) for n in re.findall(r'坑清单\s*(\d+)\s*条', readme))
declared |= set(int(n) for n in re.findall(r'（(\d+)\s*条实测坑）', readme))
check(f'README 坑计数声明 == 实际 {len(nums)} 条',
      declared == {len(nums)},
      f'声明值 {sorted(declared)} != 实际 {len(nums)}' if declared != {len(nums)} else '')

# ---------- 5. README 文件树覆盖 references 全部文档 ----------
readme_md_names = set(re.findall(r'([a-z0-9][a-z0-9-]*\.md)', readme))
ref_names = {f for f in os.listdir(os.path.join(ROOT, 'references'))
             if f.endswith('.md')}
not_in_readme = sorted(ref_names - readme_md_names)
check('README 文件树覆盖 references 全部文档', not not_in_readme,
      f'README 未提及: {not_in_readme}' if not_in_readme else '')

print()
if failures:
    print(f"CI 结果: FAIL（{len(failures)} 项未过）")
    sys.exit(1)
print('CI 结果: PASS（全部通过）')
