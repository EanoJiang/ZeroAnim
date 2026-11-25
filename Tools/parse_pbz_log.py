"""Parse an Unreal (PBZ) log file into animation/traversal event records.

UE log lines look like:
    [2026.11.25-05.30.00:123][  1234]LogPBZFallRise: [FALLRM-APPLY] frame=1234 ...

The second bracket is the engine frame number, which is the alignment key the
calibration needs. Outputs JSONL (every interesting line) and a flat CSV.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

LINE_RE = re.compile(r"^\[(?P<time>[^\]]*)\]\[\s*(?P<frame>\d+)\](?P<cat>[A-Za-z0-9_.]+):\s?(?P<msg>.*)$")

# Event tags that carry traversal / animation state.
EVENT_TAGS = (
    "[FALLRM-APPLY]",
    "[FALLRISE]",
    "[FALLIMPACT]",
    "[PHYSFALL]",
    "[RMCAP]",
    "[ZBOOST]",
    "[MontageLoss]",
    "[SeqAnimModeProbe]",
    "[SwitchApproach]",
    "[LionPlayerPolePlacement]",
    "[GetSocketWorldTransformForAnim",
    "[PBZTrace]",
)

NUM = r"[-+]?\d+(?:\.\d+)?"
KV_RE = re.compile(r"(?P<key>[A-Za-z_][A-Za-z0-9_.]*)=(?P<val>\([^)]*\)|\[[^\]]*\]|\"[^\"]*\"|[^\s|]+)")


def parse_kv(msg: str) -> dict[str, str]:
    out = {}
    for m in KV_RE.finditer(msg):
        out[m.group("key")] = m.group("val")
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("--out-csv", type=Path)
    ap.add_argument("--out-jsonl", type=Path)
    ap.add_argument("--all", action="store_true", help="emit every line, not just known events")
    args = ap.parse_args()

    rows = []
    cats = {}
    total = 0
    with args.log.open("r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            total += 1
            line = line.rstrip("\r\n")
            m = LINE_RE.match(line)
            if not m:
                continue
            frame = int(m.group("frame"))
            cat = m.group("cat")
            msg = m.group("msg")
            cats[cat] = cats.get(cat, 0) + 1
            interesting = any(tag in msg for tag in EVENT_TAGS)
            if not interesting and not args.all:
                continue
            kv = parse_kv(msg)
            rows.append(
                {
                    "time": m.group("time"),
                    "frame": frame,
                    "category": cat,
                    "event": next((t.strip("[]") for t in EVENT_TAGS if t in msg), ""),
                    "montage": kv.get("montage", ""),
                    "section": kv.get("section", ""),
                    "t": kv.get("t", ""),
                    "dt": kv.get("dt", ""),
                    "vz": kv.get("Vz", ""),
                    "gain": kv.get("gain", ""),
                    "anim_rm": kv.get("AnimRM", ""),
                    "override_rm": kv.get("OverrideRM", kv.get("Override", "")),
                    "additive": kv.get("Additive", ""),
                    "active_rm_src": kv.get("ActiveRMSrc", ""),
                    "loc": kv.get("loc", ""),
                    "vel": kv.get("Vel", ""),
                    "speed": kv.get("speed", ""),
                    "dz": kv.get("dZ", ""),
                    "raw": msg,
                }
            )

    print(f"lines={total} matched={len(rows)}")
    print("categories seen:")
    for c, n in sorted(cats.items(), key=lambda kv: -kv[1]):
        print(f"  {n:6d} {c}")

    if args.out_jsonl:
        with args.out_jsonl.open("w", encoding="utf-8") as fh:
            for r in rows:
                fh.write(json.dumps(r, ensure_ascii=False) + "\n")
        print(f"wrote {args.out_jsonl}")

    if args.out_csv:
        cols = ["time", "frame", "category", "event", "montage", "section", "t", "dt",
                "vz", "gain", "anim_rm", "override_rm", "additive", "active_rm_src",
                "loc", "vel", "speed", "dz", "raw"]
        with args.out_csv.open("w", encoding="utf-8-sig", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=cols)
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {args.out_csv}")

    if rows:
        print(f"frame range: {rows[0]['frame']}..{rows[-1]['frame']}")
        print(f"time  range: {rows[0]['time']} .. {rows[-1]['time']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
