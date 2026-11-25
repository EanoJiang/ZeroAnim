"""Find the TraceLog channel list + trace file naming used by UPBZTraceSystem.

Read-only. UPBZTraceSystem logs "Starting File Trace, Channels: %s". The %s is a
hardcoded channel preset stored somewhere in .rdata. Locate it, and locate the
.utrace output path format.
"""
from __future__ import annotations

import re
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")

PATTERNS = [
    r"cpu,gpu",
    r"gpu,frame",
    r"bookmark,",
    r",animation",
    r"animation,",
    r"Object,Animation",
    r"\.utrace",
    r"TraceData",
    r"Profiling/CSV",
    r"Profiling\\\\",
]


def main() -> int:
    data = EXE.read_bytes()

    for pat in PATTERNS:
        print(f"\n===== wide /{pat}/ =====")
        for m in re.finditer(pat.encode("utf-16-le"), data):
            lo = max(0, m.start() - 160)
            hi = min(len(data), m.start() + 240)
            chunk = data[lo:hi]
            txt = "".join(
                chr(chunk[i]) if 32 <= chunk[i] < 127 and chunk[i + 1] == 0 else "."
                for i in range(0, len(chunk) - 1, 2)
            )
            print(f"  @0x{m.start():08x} {txt!r}")

        print(f"===== ascii /{pat}/ =====")
        for m in re.finditer(pat.encode(), data):
            lo = max(0, m.start() - 120)
            hi = min(len(data), m.start() + 200)
            chunk = data[lo:hi]
            txt = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            print(f"  @0x{m.start():08x} {txt!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
