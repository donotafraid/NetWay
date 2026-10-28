#!/usr/bin/env bash
# scripts/run_slo_bench.sh -- SLO 基准：独立 server、绑核、5 次中位数、归档
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN_LAT=./build-bench/bench-build/bench_latency
BIN_SRV=./build-bench/bench-build/bench_server
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=bench-results/${STAMP}_slo
mkdir -p "$OUT"

RUNS=${RUNS:-5}
OPS=${OPS:-100000}
BATCH=${BATCH:-16}
SRV_PORT=${SRV_PORT:-4892}
# 4268 / 2864 是历史固定负载，不是“当前容量的 80%”。去日志后 8T 共享容量
# 约 7032 calls/s，4268 只有约 61%；名字 RATE_80 仅为兼容已有归档脚本。
# 换机器/换构建后必须重新标定，不要把这两个值当成线性扩展承诺。
RATE_80=${RATE_80:-4268}           # historical fixed load, not 80% of 7032
RATE_80_PTC=${RATE_80_PTC:-2864}   # historical fixed load, not 80% of 21600

# ---- 绑核：server 独占 cpu7，client 用 0/2/4/6（4 个独立物理核）----
SRV_CPUS=${SRV_CPUS:-7}
C1_CPUS=${C1_CPUS:-0}
C4_CPUS=${C4_CPUS:-0,2,4,6}
C8_CPUS=${C8_CPUS:-0-7}

# ---- 环境归档 ----
{
  date -Is
  echo "--- git ---"; git rev-parse HEAD 2>/dev/null || echo "no git"
  git status --short 2>/dev/null || true
  echo "--- cpu ---"; lscpu | grep -E "Model name|Core|Thread|NUMA"
  grep "cpu MHz" /proc/cpuinfo | head -8
  echo "--- bin vs src ---"
  ls -l --time-style=full-iso "$BIN_LAT" "$BIN_SRV" src/OPCUAPacking/ua.cpp
  echo "--- config ---"; sed -n '546p;624p' src/OPCUAPacking/ua.cpp
  echo "--- params ---"
  echo "RUNS=$RUNS OPS=$OPS BATCH=$BATCH SRV_CPUS=$SRV_CPUS RATE_80=$RATE_80"
} > "$OUT/env.txt" 2>&1

# ---- 启动独立 server ----
taskset -c "$SRV_CPUS" "$BIN_SRV" --port "$SRV_PORT" > "$OUT/server.log" 2>&1 &
SRV_PID=$!
trap 'kill $SRV_PID 2>/dev/null || true' EXIT

for _ in $(seq 1 100); do
  grep -q '^READY ' "$OUT/server.log" 2>/dev/null && break
  sleep 0.1
done
grep -q '^READY ' "$OUT/server.log" || { echo "server not ready"; exit 3; }
ENDPOINT="opc.tcp://127.0.0.1:${SRV_PORT}"

# ---- 跑一组：<label> <cpus> <threads> [rate] [extra_flag] ----
run_one() {
  local label=$1 cpus=$2 threads=$3 rate=${4:-0} flag=${5:-}
  local extra=()
  [ "$rate" != "0" ] && extra+=(--rate "$rate")
  [ -n "$flag" ] && extra+=("$flag")
  echo "=== $label: threads=$threads cpus=$cpus rate=${rate} flag=${flag:-none} ==="
  for i in $(seq 1 "$RUNS"); do
    # Keep stdout machine-readable. SDK/client logs go to a separate file; the
    # client log level is pinned to ERROR in bench_latency, so this is normally
    # empty and the 1.6 GB stderr problem cannot recur unnoticed.
    taskset -c "$cpus" "$BIN_LAT" --endpoint "$ENDPOINT" \
      --threads "$threads" --ops "$OPS" --batch "$BATCH" --repeats 1 \
      "${extra[@]}" > "$OUT/${label}_run${i}.txt" \
      2> "$OUT/${label}_run${i}.stderr.log" || true
    echo "  run $i: $(grep -o 'p99=[0-9.]*' "$OUT/${label}_run${i}.txt" || echo 'FAIL')"
  done
}

# ---- 5 组：共享 client 1T/4T/8T + 8T 80% 限速 + 8T 每线程独立 client ----
run_one 1t      "$C1_CPUS" 1
run_one 4t      "$C4_CPUS" 4
run_one 8t      "$C8_CPUS" 8
run_one 8t_r80  "$C8_CPUS" 8 "$RATE_80"
run_one 8t_ptc  "$C8_CPUS" 8 0 --per-thread-client
run_one 8t_ptc_r80 "$C8_CPUS" 8 "$RATE_80_PTC" --per-thread-client

# ---- 中位数汇总（动态发现所有 label）----
med() { sort -n | awk '{a[NR]=$1} END{if(NR%2)print a[(NR+1)/2];else print (a[NR/2]+a[NR/2+1])/2}'; }
LABELS=$(ls "$OUT"/*_run1.txt 2>/dev/null | xargs -n1 basename | sed 's/_run1.txt//' | sort)
{
  for label in $LABELS; do
    echo "--- $label (median over $RUNS runs) ---"
    for m in p50 p90 p99 p999 max; do
      echo -n "$m: "
      grep -ho "${m}=[0-9.]*" "$OUT/${label}_run"*.txt | cut -d= -f2 | med
    done
    echo -n "calls_s: "
    grep -ho "calls_s=[0-9.]*" "$OUT/${label}_run"*.txt | cut -d= -f2 | med
    echo
  done
} | tee "$OUT/median.txt"

# 空 stderr 文件不留在归档里；非空说明有 warning，保留供排查。
find "$OUT" -name '*_run*.stderr.log' -empty -delete 2>/dev/null || true

# ---- bootstrap 95% CI（p99 / p999，所有 label）----
python3 scripts/bootstrap_ci.py "$OUT" | tee "$OUT/ci.txt"

echo "归档: $OUT/"