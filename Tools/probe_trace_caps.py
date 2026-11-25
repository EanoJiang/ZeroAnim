"""Check which Trace (Unreal Insights) entry points survive in the shipping PBZ.exe.

Read-only string probe. Answers: is `-trace`/`-tracefile` compiled in, and are the
`Trace.Start` / `Trace.Stop` console commands reachable, or was this build shipped
with tracing compiled out (UE_BUILD_SHIPPING)?
"""
from __future__ import annotations

from pathlib import Path

EXE = Path(r"E:\SteamLibrary\steamapps\common\PBZ\platform\steam\PBZ.exe")

NAMES = [
    "Trace.Start", "Trace.Stop", "Trace.Status", "Trace.Bookmark", "Trace.Snapshot",
    "Trace.Send", "Trace.Record", "Trace.Pause", "Trace.Resume",
    "TracingToFile", "TracingToServer", "TracingToSecureNetwork", "TracingToCustomRelay",
    "TraceToFile", "TraceServer", "TraceService", "TraceClient", "TraceStore",
    "-trace", "-tracefile", "-tracehost", "-trace=default", "utrace",
    "TraceLog", "TraceAnalysis", "TraceControl", "TraceAuxiliary", "FTraceAuxiliary",
    "TraceDataStream", "FChannel", "TraceSystem", "TraceWriter",
    "Full tracing in shipping is not supported",
    "TraceFile", "StartTrace", "StopTrace", "TraceLog.Bookmark",
    "Trace.StartFile", "Trace.StartServer",
]


def main() -> int:
    data = EXE.read_bytes()
    print(f"exe = {EXE}")
    print(f"size = {len(data):,}\n")
    print(f"{'name':46} {'ascii':>6} {'wide':>6}")
    print("-" * 62)
    for n in NAMES:
        a = data.count(n.encode())
        w = data.count(n.encode("utf-16-le"))
        print(f"{n:46} {a:6d} {w:6d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
