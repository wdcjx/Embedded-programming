#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
validate_generated.py — STM32 标准库（SPL）生成代码工程纪律检查（启发式静态检查）

用法：
    python validate_generated.py main.c bsp_usart.c ...     # 检查指定 .c 文件
    python validate_generated.py --dir output/              # 递归检查目录下全部 .c
    python validate_generated.py --dir . --density hd       # HD/XL 密度向量表

退出码：0 = 无 VIOLATION（WARNING 允许存在）
        1 = 存在 VIOLATION（必须全部修复后才能交付）
        2 = 用法错误 / 无文件可检查

说明：
  - 启发式检查：WARNING 需人工逐条确认；本工具不能替代编译与上板验证。
  - 多文件工程请把相关 .c 一起传入（时钟使能与外设初始化可能分属不同文件）。
  - 自测：python validate_generated.py ../tests/bad_sample.c 应报多条 VIOLATION。
"""

import argparse
import re
import sys
from pathlib import Path

VIOL = "VIOLATION"
WARN = "WARNING "

KEYWORDS = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "break",
    "continue", "return", "goto", "sizeof", "static", "volatile", "const",
    "void", "int", "char", "long", "short", "unsigned", "signed", "float",
    "double", "struct", "union", "enum", "typedef", "extern", "register",
}

CORE_HANDLERS = {
    "NMI_Handler", "HardFault_Handler", "MemManage_Handler", "BusFault_Handler",
    "UsageFault_Handler", "SVC_Handler", "DebugMon_Handler", "PendSV_Handler",
    "SysTick_Handler",
}

MD_VECTORS = CORE_HANDLERS | {
    "WWDG_IRQHandler", "PVD_IRQHandler", "TAMPER_IRQHandler", "RTC_IRQHandler",
    "FLASH_IRQHandler", "RCC_IRQHandler", "EXTI0_IRQHandler", "EXTI1_IRQHandler",
    "EXTI2_IRQHandler", "EXTI3_IRQHandler", "EXTI4_IRQHandler",
    "DMA1_Channel1_IRQHandler", "DMA1_Channel2_IRQHandler", "DMA1_Channel3_IRQHandler",
    "DMA1_Channel4_IRQHandler", "DMA1_Channel5_IRQHandler", "DMA1_Channel6_IRQHandler",
    "DMA1_Channel7_IRQHandler", "ADC1_2_IRQHandler",
    "USB_HP_CAN1_TX_IRQHandler", "USB_LP_CAN1_RX0_IRQHandler",
    "CAN1_RX1_IRQHandler", "CAN1_SCE_IRQHandler", "EXTI9_5_IRQHandler",
    "TIM1_BRK_IRQHandler", "TIM1_UP_IRQHandler", "TIM1_TRG_COM_IRQHandler",
    "TIM1_CC_IRQHandler", "TIM2_IRQHandler", "TIM3_IRQHandler", "TIM4_IRQHandler",
    "I2C1_EV_IRQHandler", "I2C1_ER_IRQHandler", "I2C2_EV_IRQHandler",
    "I2C2_ER_IRQHandler", "SPI1_IRQHandler", "SPI2_IRQHandler",
    "USART1_IRQHandler", "USART2_IRQHandler", "USART3_IRQHandler",
    "EXTI15_10_IRQHandler", "RTC_Alarm_IRQHandler", "USBWakeUp_IRQHandler",
}

HD_XL_EXTRA = {
    "FSMC_IRQHandler", "SDIO_IRQHandler", "TIM5_IRQHandler", "SPI3_IRQHandler",
    "UART4_IRQHandler", "UART5_IRQHandler", "TIM6_IRQHandler", "TIM7_IRQHandler",
    "DMA2_Channel1_IRQHandler", "DMA2_Channel2_IRQHandler", "DMA2_Channel3_IRQHandler",
    "DMA2_Channel4_IRQHandler", "DMA2_Channel5_IRQHandler",
    "ADC3_IRQHandler", "TIM8_BRK_IRQHandler", "TIM8_UP_IRQHandler",
    "TIM8_TRG_COM_IRQHandler", "TIM8_CC_IRQHandler",
}

FUNC_RE = re.compile(
    r"(?m)^(?:static\s+)?[A-Za-z_]\w*[\s\*]+(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}]*)\)\s*\{"
)
DECL_RE = re.compile(
    r"(?m)^(?:static\s+)?(?:const\s+)?(?P<vol>volatile\s+)?"
    r"(?:unsigned\s+|signed\s+)?(?:[A-Za-z_]\w*[\s\*]+)+"
    r"(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;\n]*)?;"
)
BLOCK_CALL_RE = re.compile(r"(?i)\b(delay\w*|hal_delay|printf|puts|sprintf|snprintf|scanf|gets)\s*\(")
FLAG_COND_RE = re.compile(
    r"GetFlagStatus|GetITStatus|CheckEvent|GetCalibrationStatus|GetResetCalibrationStatus"
)

# (初始化调用, 外设标签, 对应时钟使能宏片段)——同一行兼容 SPL 与 HAL 两种命名
PERIPHERALS = [
    (r"\b(?:HAL_)?GPIO_Init\s*\(", "GPIO", r"RCC_APB2Periph_GPIO"),
    (r"\b(?:HAL_)?(?:UART|USART)_Init\s*\(", "USART", r"RCC_APB[12]Periph_USART"),
    (r"\bTIM_(TimeBaseInit|OC[1-4]Init|IC[1-4]Init)\s*\(|\bHAL_TIM_\w*Init\s*\(",
     "TIM", r"RCC_APB[12]Periph_TIM"),
    (r"\b(?:HAL_)?ADC_Init\s*\(", "ADC", r"RCC_APB2Periph_ADC"),
    (r"\b(?:HAL_)?I2C_Init\s*\(", "I2C", r"RCC_APB1Periph_I2C"),
    (r"\b(?:HAL_)?SPI_Init\s*\(", "SPI", r"RCC_APB[12]Periph_SPI"),
    (r"\b(?:HAL_)?DMA_Init\s*\(", "DMA", r"RCC_AHBPeriph_DMA"),
]

# ---- FreeRTOS（SPL+RTOS 轨）----
RTOS_DETECT_RE = re.compile(
    r"#\s*include\s*\"(?:FreeRTOS\.h|task\.h|semphr\.h|queue\.h|timers\.h|event_groups\.h)\""
    r"|\b(FreeRTOS|xQueue|xSemaphore|vTask|xTask|uxTask|pdMS_TO_TICKS|portMAX_DELAY"
    r"|portEND_SWITCHING_ISR|portYIELD|TaskHandle_t|BaseType_t|QueueHandle_t|SemaphoreHandle_t)\b"
)
# 任务级 API → FromISR 版本；None = 无 FromISR 版本，中断内绝对禁止
RTOS_TASK_API_MAP = {
    "xQueueSend": "xQueueSendFromISR",
    "xQueueSendToFront": "xQueueSendToFrontFromISR",
    "xQueuePeek": "xQueuePeekFromISR",
    "xQueueReceive": "xQueueReceiveFromISR",
    "xQueueGenericSend": "xQueueGenericSendFromISR",
    "xSemaphoreGive": "xSemaphoreGiveFromISR",
    "xSemaphoreTake": "xSemaphoreTakeFromISR",
    "xTaskNotify": "xTaskNotifyFromISR",
    "xTaskNotifyAndQuery": "xTaskNotifyAndQueryFromISR",
    "xTaskNotifyGive": "vTaskNotifyGiveFromISR",
    "xEventGroupSetBits": "xEventGroupSetBitsFromISR",
    "xStreamBufferSend": "xStreamBufferSendFromISR",
    "xMessageBufferSend": "xMessageBufferSendFromISR",
    "vTaskResume": "xTaskResumeFromISR",
    "vTaskDelay": None,
    "vTaskDelayUntil": None,
    "vTaskSuspend": None,
}

# ---- HAL（CubeMX 轨）----
HAL_DETECT_RE = re.compile(
    r'#\s*include\s*"stm32f1xx_hal\.h"'
    r"|\b(HAL_Init|HAL_Delay|HAL_GetTick|HAL_UART_Init|HAL_GPIO_Init|HAL_RCC_"
    r"|UART_HandleTypeDef|HAL_StatusTypeDef|HAL_OK)\b"
)


def blank_keep_newlines(match):
    return re.sub(r"[^\n]", " ", match.group(0))


def neutralize(text):
    """注释和字符串替换为等长空白，保留行号与偏移。"""
    text = re.sub(r"/\*.*?\*/", blank_keep_newlines, text, flags=re.S)
    text = re.sub(r"//[^\n]*", blank_keep_newlines, text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', blank_keep_newlines, text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", blank_keep_newlines, text)
    return text


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def match_brace(s, open_idx):
    depth = 0
    for i in range(open_idx, len(s)):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(s) - 1


def match_paren(s, open_idx):
    depth = 0
    for i in range(open_idx, len(s)):
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return len(s) - 1


def in_spans(pos, spans):
    return any(a <= pos <= b for a, b in spans)


def is_isr(name, params=""):
    """真中断/系统处理函数：IRQ 向量名，或无参系统 Handler。
    带参的 xxHandler（如 USARTx_RCVHandler(USART_TypeDef*)）是普通函数，不算 ISR。
    Error_Handler 是 HAL/CubeMX 惯例的错误处理函数（非向量）。"""
    if name == "Error_Handler":
        return False
    if name.endswith("_Callback"):
        # 转发回调命名（如 TIM2_IRQHandler_Callback，it.c 向量转调的业务体）：
        # 不是向量名，不做向量表校验；中断上下文纪律由 is_isr_context 的 Callback 分支覆盖。
        return False
    if name.endswith("_IRQHandler"):
        # CMSIS/startup 惯例的外设向量名（HAL_xxx_IRQHandler 库函数由库目录跳过规则豁免）
        return True
    if "IRQ" in name:
        # 含 IRQ 但不带 _IRQHandler 后缀：仅当它是某已知向量的真前缀（截断/拼写缺尾巴，
        # 如 WWDG_IRQHandle、TIM1_UP_IRQHandle）才视为疑似拼错的向量名；
        # 其余（如读 IRQ 引脚的普通函数 NRF24L01_R_IRQ）不是中断处理函数。
        all_vec = MD_VECTORS | HD_XL_EXTRA
        return any(v.startswith(name) and v != name for v in all_vec)
    return name.endswith("Handler") and params.strip() in ("", "void")


def is_isr_context(name, params=""):
    """中断上下文执行的函数：向量本身 + HAL 回调（HAL_xxx_IRQHandler 内部调用，
    运行在中断优先级上，纪律与 ISR 相同）。"""
    return is_isr(name, params) or name.endswith("Callback")


def analyze(path, text, vectors, findings, irq_owner):
    fname = path.name

    def add(line, level, msg):
        findings.append((fname, line, level, msg))

    # ---- 函数与全局变量收集 ----
    funcs = []
    for m in FUNC_RE.finditer(text):
        open_idx = m.end() - 1
        close_idx = match_brace(text, open_idx)
        funcs.append({
            "name": m.group("name"),
            "params": m.group("params"),
            "start": m.start(),
            "open": open_idx,
            "close": close_idx,
            "body": text[open_idx + 1: close_idx],
        })

    spans = sorted((f["start"], f["close"]) for f in funcs)
    globals_list = []  # (name, is_volatile, line)
    prev = 0
    for s, e in spans:
        seg = text[prev:s]
        for dm in DECL_RE.finditer(seg):
            if dm.group(0).lstrip().startswith("extern"):
                continue  # extern 声明的定义在别的文件，由定义文件负责检查
            globals_list.append(
                (dm.group("name"), bool(dm.group("vol")), line_of(text, prev + dm.start("name")))
            )
        prev = e + 1
    for dm in DECL_RE.finditer(text[prev:]):
        if dm.group(0).lstrip().startswith("extern"):
            continue
        globals_list.append(
            (dm.group("name"), bool(dm.group("vol")), line_of(text, prev + dm.start("name")))
        )

    isr_spans = [(f["open"], f["close"]) for f in funcs if is_isr_context(f["name"], f["params"])]
    rtos_used = bool(RTOS_DETECT_RE.search(text))

    # ---- 1. 中断函数名合法性 + 全工程唯一 ----
    for f in funcs:
        n = f["name"]
        if is_isr(n, f["params"]):
            line = line_of(text, f["start"])
            if n not in vectors:
                add(line, VIOL, f"未知中断/系统处理函数名 '{n}'：请逐字符对照启动文件向量表"
                                "（拼错的向量编译不报错但中断永不触发）")
            if n in irq_owner and irq_owner[n] != fname:
                add(line, VIOL, f"中断函数 '{n}' 已在 {irq_owner[n]} 中定义，全工程只能有一份")
            else:
                irq_owner[n] = fname

    # ---- 2. ISR 纪律：volatile / 阻塞调用 / while 轮询 ----
    for f in funcs:
        if not is_isr_context(f["name"], f["params"]):
            continue
        body = f["body"]
        calls = set(re.findall(r"([A-Za-z_]\w*)\s*\(", body))
        used = set(re.findall(r"[A-Za-z_]\w*", body)) - calls - KEYWORDS
        for g_name, g_vol, g_line in globals_list:
            if g_name in used and not g_vol:
                add(line_of(text, f["start"]), VIOL,
                    f"ISR '{f['name']}' 访问了非 volatile 全局变量 '{g_name}'"
                    f"（声明于本文件第 {g_line} 行）：与中断共享的变量必须 volatile")
        if rtos_used:
            for cname in sorted(calls):
                if cname in RTOS_TASK_API_MAP:
                    repl = RTOS_TASK_API_MAP[cname]
                    if repl is None:
                        add(line_of(text, f["start"]), VIOL,
                            f"ISR '{f['name']}' 调用 '{cname}()'：无 FromISR 版本，"
                            "中断内禁止延时/阻塞，应置标志由任务处理")
                    else:
                        add(line_of(text, f["start"]), VIOL,
                            f"ISR '{f['name']}' 调用了任务级 API '{cname}()'："
                            f"必须改用 '{repl}()'，并配合 portEND_SWITCHING_ISR(woken)")

        for cm in BLOCK_CALL_RE.finditer(body):
            add(line_of(text, f["open"] + 1 + cm.start()), VIOL,
                f"ISR '{f['name']}' 内调用阻塞函数 '{cm.group(1)}()'：中断内禁止延时/printf/忙等")
        for wm in re.finditer(r"\bwhile\s*\(", body):
            op2 = wm.end() - 1
            cp2 = match_paren(body, op2)
            cond2 = body[op2 + 1: cp2].strip()
            if cond2 in ("1", "0", "!0"):
                continue  # 常量死循环：Fault Handler 等场景属合法设计
            add(line_of(text, f["open"] + 1 + wm.start()), WARN,
                f"ISR '{f['name']}' 内存在 while 轮询：请确认为事件驱动或有时限保证")

    # ---- 3. 硬件等待循环必须带超时 ----
    for m in re.finditer(r"\bwhile\s*\(", text):
        op = m.end() - 1
        cp = match_paren(text, op)
        cond = text[op + 1: cp]
        if not FLAG_COND_RE.search(cond):
            continue
        if in_spans(m.start(), isr_spans):
            continue  # ISR 内的 while 已在上面单独提示
        j = cp + 1
        while j < len(text) and text[j].isspace():
            j += 1
        if j < len(text) and text[j] == "{":
            body = text[j: match_brace(text, j) + 1]
        else:
            se = text.find(";", j)
            body = text[j: se + 1] if se != -1 else ""
        if "--" not in cond + body and "-=" not in cond + body:
            add(line_of(text, m.start()), WARN,
                "硬件等待循环未见超时退出（无递减计数）：每个等待必须有超时与失败分支，"
                "若确可接受请在交付说明中给出理由")

    # ---- 4. 禁止动态内存 ----
    for m in re.finditer(r"\b(malloc|calloc|realloc|free)\s*\(", text):
        add(line_of(text, m.start()), VIOL,
            f"使用动态内存 '{m.group(1)}()'：初始化完成后禁止 malloc/free，缓冲区一律静态分配")

    # ---- 5. NVIC 优先级分组（分组全工程一次，约定集中在 main 所在文件）----
    nvic_init = re.search(r"\bNVIC_Init\s*\(", text)
    if nvic_init:
        grp = re.search(r"\bNVIC_PriorityGroupConfig\s*\(", text)
        has_main = bool(re.search(r"\b(?:int|void)\s+main\s*\(", text))
        if not grp:
            if has_main:
                add(line_of(text, nvic_init.start()), VIOL,
                    "main 所在文件使用了 NVIC_Init 但未见 NVIC_PriorityGroupConfig："
                    "上电需最先统一配置一次优先级分组")
            else:
                add(line_of(text, nvic_init.start()), WARN,
                    "使用了 NVIC_Init 但本文件未见 NVIC_PriorityGroupConfig：分组应全工程"
                    "统一配置一次（常集中在 main 所在文件），若他处已配可接受并说明")
        elif grp.start() > nvic_init.start():
            add(line_of(text, grp.start()), WARN,
                "NVIC_PriorityGroupConfig 出现在 NVIC_Init 之后：应在最前配置且全工程一次")

    # ---- 6. 时钟先开（容忍工程用宏封装时钟名；SPL 与 HAL 两种时钟宏都认）----
    any_clock = re.search(r"(\bRCC_\w*PeriphClockCmd\s*\(|__HAL_RCC_\w+_CLK_ENABLE\s*\()", text)
    # 宏间接时钟使能：如 led_drv.h 的 #define LED_CLK() __HAL_RCC_GPIOC_CLK_ENABLE()
    # 之类在别处展开的宏调用（形如 XXX_CLK()/XXX_CLOCK_EN()）。单文件分析看不见宏体，
    # 不能当确凿证据清零，只把 VIOLATION 降为 WARNING 提示人工核对宏定义。
    macro_clock = re.search(r"\b[A-Z][A-Z0-9_]*CLK[A-Z0-9_]*\s*\(\s*\)", text)
    for init_pat, label, clk_pat in PERIPHERALS:
        inits = list(re.finditer(init_pat, text))
        if not inits:
            continue
        if not any_clock:
            if macro_clock:
                add(line_of(text, inits[0].start()), WARN,
                    f"检测到 {label} 初始化，本文件时钟使能走宏封装（{macro_clock.group(0).strip()}）："
                    "宏体在头文件/别处，请人工核对其中确含 RCC 时钟使能")
            else:
                add(line_of(text, inits[0].start()), VIOL,
                    f"检测到 {label} 初始化，但本文件未见任何 RCC_...PeriphClockCmd / "
                    "__HAL_RCC_..._CLK_ENABLE 时钟使能：时钟必须最先开启")
            continue
        # 时钟顺序只在同一函数内比较：方向切换等辅助函数可能排在 init 之前，
        # 文件级"首 RCC 行 vs 首初始化行"比较会误报（真实工程 Health_watch iic.c 教训）
        for im in inits:
            owner = next((f for f in funcs if f["start"] <= im.start() <= f["close"]), None)
            if owner is None:
                continue
            clks = [c for c in re.finditer(clk_pat, text)
                    if owner["start"] <= c.start() <= owner["close"]]
            if clks and clks[0].start() > im.start():
                add(line_of(text, clks[0].start()), WARN,
                    f"{label} 时钟使能出现在初始化之后：应先开时钟再配置")
    if re.search(r"\bEXTI_Init\s*\(|\bGPIO_EXTILineConfig\s*\(", text):
        if not any_clock and "RCC_APB2Periph_AFIO" not in text:
            add(1, VIOL, "使用了 EXTI/引脚重映射，但未见 RCC_APB2Periph_AFIO 时钟使能")
        elif "AFIO" not in text:
            add(1, WARN, "使用了 EXTI/引脚重映射：请确认 AFIO 时钟已在某处使能"
                         "（时钟名被宏封装时 RCC_APB2Periph_AFIO 容易漏配）")

    # ---- 7. printf 重定向 ----
    if re.search(r"\bprintf\s*\(", text) and \
            not re.search(r"\b(fputc|_write|__io_putchar)\b", text):
        add(1, WARN, "使用了 printf 但未见 fputc/_write/__io_putchar 重定向"
                     "（Keil 需勾 MicroLIB）")

    # ---- 8. RTOS（SPL+FreeRTOS）专项 ----
    if rtos_used:
        for m in re.finditer(r"\bSysTick_Config\s*\(", text):
            add(line_of(text, m.start()), VIOL,
                "FreeRTOS 工程调用 SysTick_Config()：SysTick 已由内核接管作 tick 源，"
                "裸机节拍（delay_init/millis/SysTick_Handler）必须整体移除，任务延时用 vTaskDelay()")
        gm = re.search(r"NVIC_PriorityGroupConfig\s*\(\s*NVIC_PriorityGroup_(\d)", text)
        if gm and gm.group(1) != "4":
            add(line_of(text, gm.start()), WARN,
                "FreeRTOS 工程应使用 NVIC_PriorityGroup_4（全 4 位抢占）："
                "configMAX_SYSCALL_INTERRUPT_PRIORITY 的优先级语义依赖全抢占分组")
        if HAL_DETECT_RE.search(text):
            add(1, WARN,
                "RTOS+HAL 组合：请确认 CubeMX 的 TimeBase Source 已改为非 SysTick 的 TIM"
                "（HAL 时基占用 SysTick 会与内核冲突）；任务延时用 osDelay/vTaskDelay，"
                "中断/回调内禁止 HAL_Delay")


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    ap = argparse.ArgumentParser(description="STM32 SPL 生成代码工程纪律检查（启发式）")
    ap.add_argument("files", nargs="*", help="要检查的 .c 文件（多文件工程请一起传入）")
    ap.add_argument("--dir", help="递归检查该目录下所有 .c 文件")
    ap.add_argument("--density", choices=["md", "hd", "xl"], default="md",
                    help="F103 密度（决定合法向量表），默认 md（C8T6 等）")
    args = ap.parse_args(argv)

    paths = [Path(p) for p in args.files]
    if args.dir:
        paths.extend(sorted(p for p in Path(args.dir).rglob("*.c")))
    if not paths:
        ap.error("请提供至少一个 .c 文件或 --dir 目录")

    vectors = set(MD_VECTORS)
    if args.density in ("hd", "xl"):
        vectors |= HD_XL_EXTRA

    findings = []  # (file, line, level, msg)
    irq_owner = {}
    checked = 0
    skipped = 0
    # 库/中间件目录不检查：时钟使能由应用层负责，厂商库源码没有 RCC 调用是正常的
    SKIP_DIR_PARTS = ("libraries", "stm32f10x_stdperiph_driver", "cmsis",
                      "hal_driver", "middlewares", "stdperiph")
    for p in paths:
        if args.dir and any(part.lower() in SKIP_DIR_PARTS for part in p.parts):
            skipped += 1
            continue
        if not p.is_file():
            findings.append((str(p), 0, WARN, "文件不存在，已跳过"))
            continue
        try:
            raw = p.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            findings.append((str(p), 0, WARN, f"无法读取：{exc}"))
            continue
        checked += 1
        analyze(p, neutralize(raw), vectors, findings, irq_owner)

    by_file = {}
    for f, line, level, msg in findings:
        by_file.setdefault(f, []).append((line, level, msg))
    for f in sorted(by_file):
        print(f"[{f}]")
        for line, level, msg in sorted(by_file[f]):
            print(f"  行{line:<5} [{level}] {msg}")
    n_v = sum(1 for x in findings if x[2] == VIOL)
    n_w = sum(1 for x in findings if x[2] == WARN)
    print(f"-- 检查文件: {checked}  违规(VIOLATION): {n_v}  警告(WARNING): {n_w} --")
    if skipped:
        print(f"-- 已跳过库/中间件目录文件: {skipped}（厂商库不参与纪律检查）--")
    if checked == 0:
        print("结果: 无可检查文件")
        return 2
    if n_v:
        print("结果: FAIL —— 存在 VIOLATION，全部修复后才能交付")
        return 1
    print("结果: PASS —— 无 VIOLATION（WARNING 需逐条给出接受理由）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
