#!/usr/bin/env python3
"""Evaluate the SLO gate for tmp/result.txt (the 5-run median format).

Usage:
  python3 scripts/check_bench_slo.py tmp/result.txt --profile strict
  python3 scripts/check_bench_slo.py tmp/result.txt --profile capacity

strict   is the previous "linear scaling" gate: 8T p99 <= 3*1T p99 + RTT and
         8T throughput >= 1T throughput. The 2026-10-27 data FAILS this gate.
capacity is a deployment-specific floor for one shared, serialized client:
         8T p99 <= 50ms, 8T calls/s >= 3000 and >= 0.45*1T. It is NOT a claim
         of linear scaling; see docs/benchmark.md.

The parser understands the section/median format written by
tmp/run_slo_step1.sh, for example:

  === 1t: threads=1 cores=0 ops=100000 ===
  p99: median=0.326  values=...
  calls_s: median=6707.1  values=...
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def parse(path: Path) -> dict[str, dict[str, float]]:
    sections: dict[str, dict[str, float]] = {}
    current: str | None = None
    section_re = re.compile(r"^(?:===|---)\s*([0-9]+t)\b")
    metric_re = re.compile(r"^([a-z0-9_.]+):\s*median=([0-9.]+)")
    for line in path.read_text(encoding="utf-8").splitlines():
        m = section_re.match(line.strip())
        if m:
            current = m.group(1)
            sections.setdefault(current, {})
            continue
        if current is None:
            continue
        m = metric_re.match(line.strip())
        if m:
            sections[current][m.group(1)] = float(m.group(2))
    return sections


def require(sections: dict[str, dict[str, float]], label: str, metric: str) -> float:
    if label not in sections or metric not in sections[label]:
        raise SystemExit(f"missing {label}.{metric} in input")
    return sections[label][metric]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("--profile", choices=("strict", "capacity"), default="strict")
    ap.add_argument("--rtt-ms", type=float, default=1.0,
                    help="RTT allowance for the strict 8T p99 bound")
    args = ap.parse_args()

    s = parse(args.input)
    for label in ("1t", "8t"):
        if label not in s:
            raise SystemExit(f"missing section {label}")

    rows: list[tuple[str, str, bool]] = []

    def add(name: str, detail: str, ok: bool) -> None:
        rows.append((name, detail, ok))

    p999_1t = require(s, "1t", "p999")
    add("1T p99.9 <= 1ms", f"{p999_1t:.3f}ms", p999_1t <= 1.0)

    p99_1t = require(s, "1t", "p99")
    p99_8t = require(s, "8t", "p99")
    calls_1t = require(s, "1t", "calls_s")
    calls_8t = require(s, "8t", "calls_s")

    if args.profile == "strict":
        bound = 3.0 * p99_1t + args.rtt_ms
        add("8T p99 <= 3*1T p99 + RTT",
            f"{p99_8t:.3f}ms <= {bound:.3f}ms", p99_8t <= bound)
        add("8T calls/s >= 1T calls/s",
            f"{calls_8t:.1f} >= {calls_1t:.1f}", calls_8t >= calls_1t)
    else:
        add("8T p99 <= 50ms", f"{p99_8t:.3f}ms", p99_8t <= 50.0)
        add("8T calls/s >= 3000", f"{calls_8t:.1f}", calls_8t >= 3000.0)
        add("8T calls/s >= 0.45*1T",
            f"{calls_8t:.1f} >= {0.45 * calls_1t:.1f}",
            calls_8t >= 0.45 * calls_1t)

    width = max(len(r[0]) for r in rows)
    print(f"profile: {args.profile}")
    for name, detail, ok in rows:
        print(f"  {'PASS' if ok else 'FAIL'}  {name:<{width}}  {detail}")
    failed = [r for r in rows if not r[2]]
    print(f"\n{'PASS' if not failed else 'FAIL'}: "
          f"{len(rows) - len(failed)}/{len(rows)} checks")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
