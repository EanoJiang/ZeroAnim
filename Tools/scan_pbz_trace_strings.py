"""Scan PBZ.exe for trace/insights related strings (UTF-16LE and ASCII).

Read-only: opens the binary, searches for byte patterns, and prints the
surrounding wide/ascii strings. Used to decide whether the shipping build
has UE Trace compiled in and which command line switches it parses.
"""
from __future__ import annotations

import sys
from pathlib import Path

TARGETS = [
    "trace=",
    "tracefile=",
    "tracehost=",
    "tracebookmark=",
    "traceframe",
    "statnamedevents",
    "cpuprofilertrace",
    "TraceChannels",
    "TraceHost",
    "TraceFile",
    "TraceServer",
    "TraceSend",
    "TraceServices",
    "TraceInsights",
    "TraceAnalysis",
    "AnimationInsights",
    "UE_TRACE",
    "Bookmark",
    "Insights",
    "GameplayInsights",
    "EditorTrace",
    "UnrealInsights",
    "TraceLog",
    "TraceAuxiliary",
    "FTraceAuxiliary",
    "FPerChannel",
    "ChannelProvider",
]


def wide(pattern: str) -> bytes:
    return pattern.encode("utf-16-le")


def ascii_b(pattern: str) -> bytes:
    return pattern.encode("ascii")


def decode_wide_around(data: bytes, pos: int, width: int = 120) -> str:
    start = max(0, pos - width * 2)
    end = min(len(data), pos + width * 2)
    chunk = data[start:end]
    text = chunk.decode("utf-16-le", errors="ignore")
    return "".join(c if 32 <= ord(c) < 127 else " " for c in text)


def decode_ascii_around(data: bytes, pos: int, width: int = 100) -> str:
    start = max(0, pos - width)
    end = min(len(data), pos + width)
    chunk = data[start:end]
    return "".join(chr(b) if 32 <= b < 127 else " " for b in chunk)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: scan_pbz_trace_strings.py <PBZ.exe>")
        return 2
    path = Path(sys.argv[1])
    data = path.read_bytes()
    print(f"file={path} size={len(data):,}")

    for target in TARGETS:
        hits_w = []
        needle = wide(target)
        start = 0
        while len(hits_w) < 6:
            idx = data.find(needle, start)
            if idx < 0:
                break
            hits_w.append(idx)
            start = idx + 1
        hits_a = []
        needle_a = ascii_b(target)
        start = 0
        while len(hits_a) < 4:
            idx = data.find(needle_a, start)
            if idx < 0:
                break
            # skip trivial overlaps with wide hits
            hits_a.append(idx)
            start = idx + 1
        if not hits_w and not hits_a:
            print(f"\n### {target}: NONE")
            continue
        print(f"\n### {target}: wide={len(hits_w)} ascii={len(hits_a)}")
        for pos in hits_w[:4]:
            print(f"  [W 0x{pos:X}] ...{decode_wide_around(data, pos)}...")
        for pos in hits_a[:4]:
            print(f"  [A 0x{pos:X}] ...{decode_ascii_around(data, pos)}...")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
