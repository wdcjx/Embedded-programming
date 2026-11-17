#!/usr/bin/env bash
# 编译 CI：用 gcc-arm-none-eabi 真实编译模板与示例（-c 只编译不链接——
# 验证语法/类型/头文件依赖；链接需要启动文件与链接脚本，超出模板职责）。
#
# 环境变量（由 build.yml 注入）：
#   SPL_DIR       — STM32F10x_StdPeriph_Lib V3.6.0 检出目录（锁 ff7527a）
#   FREERTOS_DIR  — FreeRTOS-Kernel V10.5.1 检出目录（锁 d7b40db）
set -euo pipefail

CC=arm-none-eabi-gcc
CFLAGS="-mcpu=cortex-m3 -mthumb -std=gnu99 -Wall -DSTM32F10X_MD -DUSE_STDPERIPH_DRIVER"

SPL_INC="$SPL_DIR/Libraries/STM32F10x_StdPeriph_Driver/inc"
CMSIS_INC="$SPL_DIR/Libraries/CMSIS/CM3/CoreSupport"
CONF_INC="$(cd "$(dirname "$0")" && pwd)"   # stm32f10x_conf.h 所在

fail=0
build() {  # build <label> <file> <extra -I ...>
  local label="$1" file="$2"; shift 2
  echo "== $label: $file"
  if ! $CC $CFLAGS "$@" -c "$file" -o /tmp/"$(basename "$file" .c)".o; then
    echo "!! FAILED: $file"
    fail=1
  fi
}

# SPL 裸机模板 + 示例
build "spl-bare template"  templates/main.c                 -I"$SPL_INC" -I"$CMSIS_INC" -I"$CONF_INC"
build "spl-bare example"   examples/blink_uart_demo/main.c  -I"$SPL_INC" -I"$CMSIS_INC" -I"$CONF_INC"

# SPL + FreeRTOS 模板（FreeRTOSConfig.h 在 templates/）
build "spl-rtos template"  templates/main_rtos_spl.c        \
  -I"$SPL_INC" -I"$CMSIS_INC" -I"$CONF_INC" \
  -I"$FREERTOS_DIR/include" -I"$FREERTOS_DIR/portable/GCC/ARM_CM3" -Itemplates

# HAL 模板（main_hal.c）需要 STM32CubeF1 全套 HAL 头，仓库不 vendored、
# CubeF1 仓库过大（500MB+）暂不纳入 —— 首版只覆盖 SPL 双轨 + 示例，
# HAL 轨仍由 validator 六样本基线覆盖（诚实边界，见 ROADMAP v0.11.7）。

if [ "$fail" -ne 0 ]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS (3 files compiled)"
