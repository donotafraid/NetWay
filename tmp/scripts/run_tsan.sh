#!/usr/bin/env bash
# scripts/run_tsan.sh —— 在 build-tsan/ 下运行 TSan 用例
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build-tsan}"
TEST_BIN="${BUILD_DIR}/tests-build/tests"
LOG_DIR="${BUILD_DIR}/tsan_logs"

[[ -x "${TEST_BIN}" ]] || { echo "缺少 ${TEST_BIN}，请先在 ${BUILD_DIR} 下 cmake --build"; exit 2; }
mkdir -p "${LOG_DIR}"

# setarch -R 关闭 ASLR：TSan 在 ASLR 开启时更容易误报
ARCH_FLAG=""
if command -v setarch >/dev/null 2>&1; then
  ARCH_FLAG="setarch $(uname -m) -R"
fi

# 先跑并发用例；--gtest_filter 与你的命令一致
FILTER_CONCURRENCY='Client.T3_*:Client.TA2_*:Client.TC_*:ClientTest.T5_*:Client.T16_*:Client.T17_*:Client.T18_*:Client.T25_*'
FILTER_API_LEASE='Client.T27_*'
FILTER_DESTRUCTOR='Client.T8C1*'

run_one() {
  local name="$1" filter="$2"
  echo "=== TSan: ${name} (filter=${filter}) ==="
  TSAN_OPTIONS='halt_on_error=1:abort_on_error=1:exitcode=66:report_signal_unsafe=0' \
    ${ARCH_FLAG} "${TEST_BIN}" --gtest_filter="${filter}" \
    2>&1 | tee "${LOG_DIR}/${name}.log"
  return "${PIPESTATUS[0]}"
}

run_one concurrency  "${FILTER_CONCURRENCY}"
run_one api_lease    "${FILTER_API_LEASE}"
run_one destructor   "${FILTER_DESTRUCTOR}"

echo "TSan 全部通过。日志在 ${LOG_DIR}/"