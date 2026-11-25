"""Probe the PBZ encrypted log container (PBZLOGE1) and enumerate PBZ log categories.

Read-only. Two jobs:
1. List every `LogPBZ*` category that the shipping binary actually contains.
2. Inspect the container header so we can decide whether the log is decryptable
   offline (needed before any animation field can be read out of it).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")


def list_categories(data: bytes) -> None:
    cats: set[str] = set()
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){6,}", data):
        s = m.group().decode("utf-16-le", "ignore")
        for part in re.findall(r"LogPBZ[A-Za-z0-9_.]*", s):
            cats.add(part)
    print(f"=== LogPBZ* categories ({len(cats)}) ===")
    for c in sorted(cats):
        print(" ", c)


def hexdump(data: bytes, n: int = 96) -> None:
    for i in range(0, min(n, len(data)), 16):
        chunk = data[i : i + 16]
        hx = " ".join(f"{b:02x}" for b in chunk)
        tx = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{i:08x}  {hx:<48}  {tx}")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "cats":
        list_categories(EXE.read_bytes())
        return 0
    log = Path(sys.argv[1])
    data = log.read_bytes()
    print(f"file={log} size={len(data):,}")
    print(f"magic={data[:8]!r}")
    hexdump(data, 128)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
