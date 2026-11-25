"""Dump the full PBZTraceSystem / pbz.trace string region of PBZ.exe.

Read-only. Prints every wide string in the neighbourhood of the custom
UPBZTraceSystem so we can see the exact console commands, cvars and the
fields that end up in TraceData.json.
"""
from __future__ import annotations

import re
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")

REGIONS = [
    ("PBZTraceSystem", 0x0C1F8000, 0x0C202000),
    ("trace-args", 0x0C0F0000, 0x0C0F6000),
]


def main() -> int:
    data = EXE.read_bytes()
    for name, lo, hi in REGIONS:
        print(f"\n########## {name}  0x{lo:08x}..0x{hi:08x} ##########")
        idx = lo
        while idx < hi:
            m = re.compile(rb"(?:[\x20-\x7e]\x00){3,}").search(data, idx, hi)
            if not m:
                break
            s = m.group().decode("utf-16-le", "ignore")
            print(f"0x{m.start():08x}  {s!r}")
            idx = m.end()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
