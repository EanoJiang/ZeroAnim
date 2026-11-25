"""Extract wide (UTF-16LE) and ASCII strings from a binary and filter them.

Read-only helper used to determine which diagnostic features a shipping game
build actually contains. Prints unique matching strings with hit counts.
"""
from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

WIDE_RE = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")
ASCII_RE = re.compile(rb"[\x20-\x7e]{6,}")


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: scan_pbz_strings.py <binary> <substring> [substring...]")
        return 2
    path = Path(sys.argv[1])
    needles = [n.lower() for n in sys.argv[2:]]
    data = path.read_bytes()
    print(f"file={path} size={len(data):,} needles={needles}")

    wide = Counter()
    for m in WIDE_RE.finditer(data):
        s = m.group().decode("utf-16-le", errors="ignore")
        low = s.lower()
        if any(n in low for n in needles):
            wide[s] += 1
    ascii_ = Counter()
    for m in ASCII_RE.finditer(data):
        s = m.group().decode("ascii", errors="ignore")
        low = s.lower()
        if any(n in low for n in needles):
            ascii_[s] += 1

    print(f"\n=== WIDE ({len(wide)} unique) ===")
    for s, c in sorted(wide.items(), key=lambda kv: (-kv[1], kv[0].lower())):
        print(f"{c:4d}  {s}")
    print(f"\n=== ASCII ({len(ascii_)} unique) ===")
    for s, c in sorted(ascii_.items(), key=lambda kv: (-kv[1], kv[0].lower())):
        print(f"{c:4d}  {s}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
