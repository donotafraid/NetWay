#!/usr/bin/env python3
"""Remove review-process wording from test comments.

The engineering is done; what is left in the test headers is bookkeeping such as
"报告 §6.2", "审查补充", "第N轮代码审查补充用例". An interviewer reads those as
"written for a reviewer", not as product code. This script rewrites only prose
in comments; it never touches code, string literals used by assertions, or test
names. It is idempotent: run it as often as you like.

Usage:
    python3 scripts/clean_review_comments.py            # rewrite in place
    python3 scripts/clean_review_comments.py --check    # dry run, exit 1 if hits
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGETS = (
    sorted(ROOT.glob("tests/*.cpp"))
    + sorted(ROOT.glob("tests/include/*.h"))
    + sorted(ROOT.glob("tests/include/util/*.h"))
    + sorted(ROOT.glob("src/OPCUAPacking/**/*.h"))
    + sorted(ROOT.glob("src/OPCUAPacking/*.cpp"))
    + sorted(ROOT.glob("include/OPCUAPacking/**/*.h"))
)

# Ordered, conservative rewrites. Each entry is (regex, replacement).
REWRITES: list[tuple[str, str]] = [
    (r"//\s*第[一二三四五六七八九十0-9]+轮代码审查补充用例[：:]?", "// 补充用例："),
    (r"//\s*第[一二三四五六七八九十0-9]+轮补充[：:]?", "// 补充用例："),
    (r"//\s*第[一二三四五六七八九十0-9]+轮补测[：:]?", "// 补充用例："),
    (r"//\s*代码审查补充[：:]?", "// 补充用例："),
    (r"（对应审查报告[^）]*）", ""),
    (r"（见 class_s7_client_review 报告[^）]*）", ""),
    (r"（报告[^）]*）", ""),
    (r"（审查报告[^）]*）", ""),
    (r"（审查补充[^）]*）", ""),
    (r"审查报告\s*", ""),
    (r"审查补充[：:]?", "补充用例："),
    (r"审查补丁[（(][^）)]*[）)][：:]?", "补充用例："),
    (r"报告\s*§", "§"),
    (r"审查\s*§", "§"),
    (r"//\s*Fourth review round\.", "// Extra cases."),
    (r"red\s*->\s*green\s+regression", "regression"),
    (r"red\s*->\s*green", "regression"),
    (r"red→green", "回归"),
]


def clean(text: str) -> str:
    for pattern, repl in REWRITES:
        text = re.sub(pattern, repl, text)
    return text


def main() -> int:
    check = "--check" in sys.argv
    changed = 0
    hits = 0
    for path in TARGETS:
        original = path.read_text(encoding="utf-8")
        updated = clean(original)
        if updated != original:
            changed += 1
            hits += sum(
                1
                for m in re.finditer(r"审查|报告\s*§|class_s7_client_review", original)
            )
            if not check:
                path.write_text(updated, encoding="utf-8")
    verb = "would rewrite" if check else "rewrote"
    print(f"{verb} {changed} file(s); markers hit: {hits}")
    if check and changed:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
