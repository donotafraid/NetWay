#!/usr/bin/env bash
set -euo pipefail
cd /root/Cross_platform_file_transfer_tool-main/Cross_platform_file_transfer_tool-main
BIN=./build-bench/bench-build/bench_latency
OUT=/tmp/slo_step1
mkdir -p "$OUT"

RUNS=5
OPS=${OPS:-100000}      # 10 万样本，p999 约 100 个尾部点；想先快速看趋势可 OPS=20000
BATCH=16

# 记录环境（频率无法固定，至少留下证据）
{
  echo "=== env before ==="
  date -Is
  grep "cpu MHz" /proc/cpuinfo
  command -v sensors >/dev/null && sensors || echo "no sensors"
} > "$OUT/env_before.txt" 2>&1

run_one() {
  local label="$1" cores="$2" threads="$3"
  echo "=== $label: threads=$threads cores=$cores ops=$OPS ==="
  for i in $(seq 1 $RUNS); do
    taskset -c "$cores" "$BIN" --threads "$threads" --ops "$OPS" --batch "$BATCH" --repeats 1 \
      > "$OUT/${label}_run${i}.txt" 2>&1
    echo "  run $i: $(sed -n 's/.*p99=\([0-9.]*\).*/p99=\1/p' "$OUT/${label}_run${i}.txt")"
  done
}

run_one 1t 0 1
run_one 4t 0,2,4,6 4
run_one 8t 0-7 8

{
  echo "=== env after ==="
  date -Is
  grep "cpu MHz" /proc/cpuinfo
  command -v sensors >/dev/null && sensors || echo "no sensors"
} > "$OUT/env_after.txt" 2>&1

med() { sort -n | awk '{a[NR]=$1} END{if(NR%2)print a[(NR+1)/2];else print (a[NR/2]+a[NR/2+1])/2}'; }

for label in 1t 4t 8t; do
  echo
  echo "--- $label (median over $RUNS runs) ---"
  for metric in p50 p90 p99 p999 max mean; do
    vals=$(for i in $(seq 1 $RUNS); do sed -n "s/.*${metric}=\([0-9.]*\).*/\1/p" "$OUT/${label}_run${i}.txt"; done)
    echo "$metric: median=$(echo "$vals" | med)  values=$(echo $vals | tr '\n' ' ')"
  done
  tp=$(for i in $(seq 1 $RUNS); do sed -n 's/.*calls_s=\([0-9.]*\).*/\1/p' "$OUT/${label}_run${i}.txt"; done)
  echo "calls_s: median=$(echo "$tp" | med)  values=$(echo $tp | tr '\n' ' ')"
done
