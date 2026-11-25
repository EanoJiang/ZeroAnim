"""Enumerate PBZ cvar / console-command / console-device surface in the shipping exe.

Read-only. Answers "can we drive an in-game dump without a debug build?" by
listing the console plumbing and the PBZ.* cvars the binary actually contains.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")

PLUMBING = [
    "OutputDebugString", "FWindowsConsoleOutputDevice", "FOutputDeviceConsole",
    "FOutputDeviceRedirector", "GLogConsole", "-console", "ConsoleCommand",
    "UConsole", "EnableCheats", "IConsoleManager", "DumpCVars",
    "ShowDebug", "ShowDebugAnimation", "AnimNode", "UAnimInstance",
    "FAnimNode_", "AnimMontage", "UAnimMontage", "FMontageBlendInfo",
    "FAnimMontageInstance", "SlotNode", "FSlotEvaluationPose",
]


def hits(data: bytes, t: str) -> tuple[int, int]:
    return (data.count(t.encode()),
            data.count(t.encode("utf-16-le")))


def wide_strings(data: bytes, minlen: int = 4):
    pat = re.compile(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen)
    for m in pat.finditer(data):
        yield m.group().decode("utf-16-le", "ignore")


def main() -> int:
    data = EXE.read_bytes()
    print(f"exe={EXE} size={len(data):,}\n")

    print("=== console plumbing ===")
    for t in PLUMBING:
        a, w = hits(data, t)
        print(f"  {t:30} ascii={a:5} wide={w:5}")

    print("\n=== PBZ.* cvars (wide + ascii) ===")
    seen: set[str] = set()
    for s in wide_strings(data, 6):
        for m in re.findall(r"PBZ\.[A-Za-z0-9_.]+", s):
            seen.add(m)
    for s in re.finditer(rb"PBZ\.[A-Za-z0-9_.]{3,}", data):
        seen.add(s.group().decode("ascii", "ignore"))
    pbz = sorted(seen)
    print(f"  count={len(pbz)}")
    for c in pbz:
        print("   ", c)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
