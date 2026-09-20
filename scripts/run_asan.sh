#!/usr/bin/env bash
# scripts/run_asan.sh —— 在 build-asan/ 下运行 ASan + UBSan 用例
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-asan}"
TEST_BIN="${BUILD_DIR}/tests-build/tests"
LOG_DIR="${BUILD_DIR}/asan_logs"

[[ -x "${TEST_BIN}" ]] || { echo "缺少 ${TEST_BIN}，请先在 ${BUILD_DIR} 下 cmake --build"; exit 2; }
mkdir -p "${LOG_DIR}"

# ASan + UBSan 覆盖：所有错误出口 + 生命周期 + 预算
FILTER='Client.T1_*:Client.T2_*:Client.T3_*:Client.T4_*:Client.T8*:Client.TD_*:Client.T16_*:Client.T17_*:Client.T18_*:Client.T19_*:Client.T20_*:Client.T21_*:Client.T22_*:Client.T23_*:Client.T26_*:Client.T28_*:Client.T29_*:Client.T30_*:Client.T9_*:ClientTest.T2_*:ClientTest.T3_*:ClientTest.T5_*:ClientTest.TD_*:ClientConfigTest.*'

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:report_objects=1:log_path=${LOG_DIR}/asan_report" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:print_summary=1" \
  "${TEST_BIN}" --gtest_filter="${FILTER}" 2>&1 | tee "${LOG_DIR}/run.log"

echo "ASan/UBSan 全部通过。日志在 ${LOG_DIR}/"