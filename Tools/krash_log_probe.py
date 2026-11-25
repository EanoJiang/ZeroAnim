"""Brute-force probe of the Krash encrypted log container (PBZLOGE1).

Read-only. The binary embeds a 32-byte AES key next to
`KrashEncryptedLog.cpp`; the container is `PBZLOGE1` + version bytes.
We try every plausible header layout / cipher mode / counter start and
report the candidate whose plaintext is mostly printable.
"""
from __future__ import annotations

import hashlib
import itertools
import sys
from pathlib import Path

from Crypto.Cipher import AES, ChaCha20

KEY_HEX = "0d068454d8c1dd2b83b0f8839d7f0604e0d28316c6036ec1213e0beaf290760d"
KEY = bytes.fromhex(KEY_HEX)


def printable_ratio(b: bytes) -> float:
    if not b:
        return 0.0
    ok = sum(1 for c in b if 32 <= c < 127 or c in (9, 10, 13))
    return ok / len(b)


def main() -> int:
    path = Path(sys.argv[1])
    data = path.read_bytes()
    print(f"file={path}\nsize={len(data)} magic={data[:8]!r} hdr={data[:32].hex()}")

    candidates = []

    # ---- plain AES-CTR / CFB / OFB with IV taken from the header ----
    for ct_start in (8, 12, 16, 0x50, 0x20, 0x40):
        body = data[ct_start:]
        ivs = {
            "zero": b"\x00" * 16,
            "hdr0": data[0:16],
            "hdr8": data[8:24],
            "hdr16": data[16:32],
            "hdr32": data[32:48],
            "hdr48": data[48:64],
            "sec16": data[16:32],
            "sec32": data[32:48],
            "sec48": data[48:64],
        }
        for name, iv in ivs.items():
            for mode in ("CTR", "CFB", "OFB", "CBC", "ECB"):
                try:
                    c = getattr(AES, mode).new(KEY, iv) if mode != "ECB" else AES.new(KEY, AES.MODE_ECB)
                    out = c.decrypt(body[:4096])
                except Exception:
                    continue
                candidates.append((printable_ratio(out), f"AES-{mode} start={ct_start} iv={name}", out[:160]))
        for name, nonce in {"hdr0_12": data[0:12], "hdr8_12": data[8:20], "hdr16_12": data[16:28], "zero12": b"\0" * 12}.items():
            try:
                out = ChaCha20.new(key=KEY, nonce=nonce).decrypt(body[:4096])
            except Exception:
                continue
            candidates.append((printable_ratio(out), f"ChaCha20 start={ct_start} nonce={name}", out[:160]))

    # ---- key derivation from the embedded key ----
    derived = {
        "sha256(key)": hashlib.sha256(KEY).digest(),
        "sha256(key+magic)": hashlib.sha256(KEY + data[:8]).digest(),
        "sha256(magic+key)": hashlib.sha256(data[:8] + KEY).digest(),
        "sha256(key+pbzloge1/1)": hashlib.sha256(KEY + b"PBZLOGE1/1").digest(),
    }
    for dname, dkey in derived.items():
        for ct_start in (16, 0x50):
            body = data[ct_start:]
            for name, iv in {"zero": b"\0" * 16, "hdr16": data[16:32], "hdr32": data[32:48], "hdr48": data[48:64]}.items():
                try:
                    out = AES.new(dkey, AES.MODE_CTR, nonce=b"", initial_value=iv).decrypt(body[:4096])
                except Exception:
                    continue
                candidates.append((printable_ratio(out), f"{dname} AES-CTR start={ct_start} iv={name}", out[:160]))

    candidates.sort(reverse=True, key=lambda x: x[0])
    print("\n=== top candidates ===")
    for ratio, label, sample in candidates[:12]:
        print(f"{ratio:.3f} {label}")
        print("      ", sample[:150])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
