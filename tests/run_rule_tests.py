#!/usr/bin/env python3
"""规则级单测：每条 validator 规则一个最小触发文件，断言规则 ID 而非计数。

防"规则静默失效但基线全绿"：bad_sample.c 的总量断言（ci_check.py 六样本基线）
无法发现某条规则悄悄不再触发——总量凑巧不变即全绿。本测试逐规则钉死：
snippet 必须触发它对应的 [Rn]，多触发/漏触发都算失败。

跑法：python tests/run_rule_tests.py（在仓库根目录）
退出码 0=全部通过；1=有失败。
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALIDATOR = ROOT / "scripts" / "validate_generated.py"
SNIPPETS = ROOT / "tests" / "rule_snippets"

# 规则 ID -> (文件列表, 额外 CLI 参数, 期望级别 "VIOL"|"WARN")
# 聚合类规则（R2/R14/R15）需要两个文件一起传入。
# WARN 级规则（R6/R7/R11）触发后 result 仍是 PASS——期望断言按级别区分。
CASES = {
    "R1":  (["r1_bad.c"], [], "VIOL"),
    "R2":  (["r2_a.c", "r2_b.c"], [], "VIOL"),
    "R3":  (["r3_bad.c"], [], "VIOL"),
    "R4":  (["r4_bad.c"], [], "VIOL"),
    "R5":  (["r5_bad.c"], [], "VIOL"),
    "R6":  (["r6_bad.c"], [], "WARN"),
    "R7":  (["r7_bad.c"], [], "WARN"),
    "R8":  (["r8_bad.c"], [], "VIOL"),
    "R9":  (["r9_bad.c"], [], "VIOL"),
    "R10": (["r10_bad.c"], [], "VIOL"),
    "R11": (["r11_bad.c"], [], "WARN"),
    "R12": (["r12_bad.c"], [], "VIOL"),
    "R13": (["r13_bad.c"], ["--density", "md"], "VIOL"),
    "R14": (["r14_a.c", "r14_b.c"], [], "VIOL"),
    "R15": (["r15_a.c", "r15_b.c"], [], "VIOL"),
}

# R0 = 消息内容掉出规则映射表（文案改动后忘了同步 RULE_TABLE）。
# 每个 snippet 允许附带触发其他规则（最小文件难免共振，如 R10 与 R9 同现），
# 但绝不允许 R0：R0 一旦出现说明映射表失配，必须修 RULE_TABLE 或文案。


def run_case(rule_id, files, extra_args, expect_level):
    argv = [sys.executable, str(VALIDATOR), "--format", "json", *extra_args]
    for f in files:
        argv.append(str(SNIPPETS / f))
    proc = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return False, f"validator 输出不是合法 JSON: {proc.stdout[:120]!r} {proc.stderr[:120]!r}"
    rules_hit = [fd["rule"] for fd in data.get("findings", [])]
    if rule_id not in rules_hit:
        return False, f"未触发 {rule_id}（实际命中: {sorted(set(rules_hit))}）"
    if "R0" in rules_hit:
        return False, f"出现 R0（映射表失配）: {[m for m in (fd['message'] for fd in data['findings']) if fd['rule'] == 'R0']}"
    if expect_level == "VIOL":
        if data.get("result") != "FAIL":
            return False, f"VIOL 级规则结果应为 FAIL，实际 {data.get('result')}"
    else:
        if data.get("result") != "PASS":
            return False, f"WARN 级规则结果应为 PASS（带警告），实际 {data.get('result')}"
        if data.get("warnings", 0) < 1:
            return False, "WARN 级规则应产生至少 1 条 WARNING"
    return True, f"触发 {rule_id}（共 {len(rules_hit)} 条 finding）"


def main():
    failures = []
    for rule_id in sorted(CASES):
        files, extra, expect_level = CASES[rule_id]
        ok, detail = run_case(rule_id, files, extra, expect_level)
        mark = "PASS" if ok else "FAIL"
        print(f"[{mark}] 规则 {rule_id}: {detail}")
        if not ok:
            failures.append(rule_id)
    print(f"\n规则级单测: {len(CASES) - len(failures)}/{len(CASES)} 通过")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
