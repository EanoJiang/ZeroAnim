"""Dump the surrounding strings for every '-trace' / 'TraceControl' hit in PBZ.exe.

Read-only. Tells us whether '-trace' is a real TraceAuxiliary switch or just a
substring of something unrelated (e.g. '-traceallocs', 'FTraceControlChannelsSet').
"""
from __future__ import annotations

import re
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")

TARGETS = [b"-trace", b"TraceControl", b"TraceService", b"TraceServer", b"TraceFile", b"utrace", b"TraceSystem"]


def extract_ascii(data: bytes, idx: int, radius: int = 80) -> str:
    lo = max(0, idx - radius)
    hi = min(len(data), idx + radius)
    chunk = data[lo:hi]
    return "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)


def extract_wide(data: bytes, idx: int, radius: int = 80) -> str:
    # idx points at the start of a utf-16-le match; widen the window.
    lo = max(0, idx - radius * 2)
    hi = min(len(data), idx + radius * 2)
    chunk = data[lo:hi]
    chars = []
    for i in range(0, len(chunk) - 1, 2):
        lo_b, hi_b = chunk[i], chunk[i + 1]
        if 32 <= lo_b < 127 and hi_b == 0:
            chars.append(chr(lo_b))
        else:
            chars.append(".")
    return "".join(chars)


def main() -> int:
    data = EXE.read_bytes()
    for tgt in TARGETS:
        print(f"\n===== {tgt.decode()!r} (ascii) =====")
        for m in re.finditer(re.escape(tgt), data):
            print(f"  @0x{m.start():08x}  {extract_ascii(data, m.start())!r}")
        print(f"===== {tgt.decode()!r} (wide) =====")
        for m in re.finditer(re.escape(tgt.decode().encode("utf-16-le")), data):
            print(f"  @0x{m.start():08x}  {extract_wide(data, m.start())!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
