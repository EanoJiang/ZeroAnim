"""Dump PBZ-specific trace-system strings + all '-trace*' switch literals.

Read-only. Goals:
  * List every string mentioning PBZTrace / ALLOW_TRACE_IN_SHIPPING so we know
    what the shipped binary actually implements.
  * Show the exact switch literals that start with '-trace' (wide + ascii).
"""
from __future__ import annotations

import re
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")


def wide_strings(data: bytes, minlen: int = 4):
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data):
        yield m.start(), m.group().decode("utf-16-le", "ignore")


def ascii_strings(data: bytes, minlen: int = 6):
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        yield m.start(), m.group().decode("ascii", "ignore")


def main() -> int:
    data = EXE.read_bytes()

    print("===== wide strings containing 'Trace' (command-ish) =====")
    seen = set()
    for off, s in wide_strings(data, 5):
        if "Trace" not in s and "trace" not in s:
            continue
        if s in seen:
            continue
        # keep only short, command-like strings
        if len(s) <= 90 and (s.startswith("-") or s.startswith("Trace") or s.startswith("PBZ")
                             or s.startswith("RewindDebugger") or s.startswith("Log") or "Trace." in s
                             or s.startswith("FTrace") or s.startswith("UPBZTrace")):
            seen.add(s)
            print(f"  @0x{off:08x}  {s!r}")

    print("\n===== ascii strings starting with '-trace' =====")
    for off, s in ascii_strings(data, 5):
        if s.startswith("-trace") and len(s) <= 80:
            print(f"  @0x{off:08x}  {s!r}")

    print("\n===== wide strings starting with '-trace' =====")
    for off, s in wide_strings(data, 5):
        if s.startswith("-trace") and len(s) <= 80:
            print(f"  @0x{off:08x}  {s!r}")

    print("\n===== ALLOW_TRACE_IN_SHIPPING context =====")
    for needle in ("ALLOW_TRACE_IN_SHIPPING", "allow-trace", "PBZTrace]", "PBZTraceSystem", "[PBZTrace"):
        for enc, label in ((needle.encode(), "ascii"), (needle.encode("utf-16-le"), "wide")):
            for m in re.finditer(re.escape(enc), data):
                lo = max(0, m.start() - 200)
                hi = min(len(data), m.start() + 400)
                chunk = data[lo:hi]
                if label == "wide":
                    txt = "".join(
                        chr(chunk[i]) if 32 <= chunk[i] < 127 and chunk[i + 1] == 0 else "."
                        for i in range(0, len(chunk) - 1, 2)
                    )
                else:
                    txt = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print(f"\n[{label}] {needle!r} @0x{m.start():08x}")
                print("   ", txt)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
