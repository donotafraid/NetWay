#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${OPEN62541_ROOT:=/root/open62541-v1.4.14-noSanitizer}"
export OPEN62541_ROOT
LOGDIR=sanitizer-logs
mkdir -p "$LOGDIR"

CONAN_TOOLCHAIN=conan_debug_build/conan_toolchain.cmake

run_one () {
  local name="$1"       # asan / lsan / tsan
  local build="build-$name"
  local enable_opt="$2" # ENABLE_ASAN / ENABLE_LSAN / ENABLE_TSAN

  echo
  echo "======================================================"
  echo " $name  ($enable_opt=ON)"
  echo "======================================================"

  cmake -B "$build" -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="$CONAN_TOOLCHAIN" \
    -DOPEN62541_ROOT="$OPEN62541_ROOT" \
    -D"${enable_opt}=ON"

  cmake --build "$build" --target tests -j"$(nproc)"

  local script="scripts/run_${name}.sh"
  if [ -x "$script" ]; then
    echo "==> 调用 $script"
    "$script" 2>&1 | tee "$LOGDIR/${name}.log"
  else
    echo "==> 未找到 $script，直接 ctest"
    ctest --test-dir "$build" --output-on-failure 2>&1 | tee "$LOGDIR/${name}.log"
  fi
}

run_one asan ENABLE_ASAN
run_one lsan ENABLE_LSAN
run_one tsan ENABLE_TSAN

echo
echo "==> 汇总"
for f in "$LOGDIR"/*.log; do
  echo "--- $f ---"
  grep -E '\[  PASSED  \]|\[  FAILED  \]|LeakSanitizer|ERROR: AddressSanitizer|WARNING: ThreadSanitizer' "$f" || true
done