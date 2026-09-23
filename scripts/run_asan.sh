#!/usr/bin/env bash
# scripts/run_asan.sh —— 在 build-asan/ 下运行 ASan + UBSan 用例
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-asan}"
TEST_BIN="${BUILD_DIR}/tests-build/tests"
LOG_DIR="${BUILD_DIR}/asan_logs"

[[ -x "${TEST_BIN}" ]] || { echo "缺少 ${TEST_BIN}，请先在 ${BUILD_DIR} 下 cmake --build"; exit 2; }
mkdir -p "${LOG_DIR}"

ARCH_FLAG="${ARCH_FLAG:-}"

# ASan + UBSan 覆盖：所有错误出口 + 生命周期 + 预算
FILTER='Client.T1_*:Client.T2_*:Client.T3_*:Client.T4_*:Client.T8*:Client.TD_*:Client.T16_*:Client.T17_*:Client.T18_*:Client.T19_*:Client.T20_*:Client.T21_*:Client.T22_*:Client.T23_*:Client.T26_*:Client.T28_*:Client.T29_*:Client.T30_*:Client.T9_*:ClientTest.T2_*:ClientTest.T3_*:ClientTest.T4_*:ClientTest.T5_*:ClientTest.T9_*:ClientTest.TD_*:Client.TA2_*:Client.TC_*:Client.T25_*:Client.T27_*:ClientConfigTest.*:Client.T34_*:Client.T35_*:Client.T36_*:Client.T37_*:Client.T38_*:Client.T39_*:Client.T40_*:Client.T41_*:Client.T42_*:Client.T43_*:Client.T44_*:Client.T45_*:Client.T46_*:Client.T47_*:Client.T48_*:Client.T49_*:Client.T50_*:Client.T51_*:Client.T52_*:Client.T53_*:Client.T54_*:Client.T55_*'
FILTER_CHECK_CONNECTED='Client.T31_*:Client.T32_*:Client.T33_*'


run_one() {
local name="$1" filter="$2"
echo "=== ASan/UBSan: ${name} (filter=${filter}) ==="

local n
n=$("${TEST_BIN}" --gtest_list_tests --gtest_filter="${filter}" 2>/dev/null \
      | grep -cE '^\s+\S' || true)
if [[ "${n}" -eq 0 ]]; then
  echo "ERROR: filter '${filter}' matched 0 tests" >&2
  return 1
fi
echo "  -> ${n} tests selected"

ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:report_objects=1:log_path=${LOG_DIR}/asan_report_${name}" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:print_summary=1" \
  ${ARCH_FLAG} "${TEST_BIN}" \
    --gtest_filter="${filter}" \
    2>&1 | tee "${LOG_DIR}/asan_${name}.log"

return "${PIPESTATUS[0]}"
}

run_one Normal  "${FILTER}"
run_one checkConnected  "${FILTER_CHECK_CONNECTED}"
echo "ASan/UBSan 全部通过。日志在 ${LOG_DIR}/"