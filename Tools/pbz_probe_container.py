"""Probe the PBZLOGE1 container: is it compressed, encrypted, or both?

Read-only. Tries raw LZ4 / zlib / zstd frame magic, then a small set of
AES/ChaCha20 key+nonce candidates harvested from the shipping binary.
Prints any candidate that yields mostly-printable output.
"""
from __future__ import annotations

import sys
from pathlib import Path

LOG = Path(sys.argv[1] if len(sys.argv) > 1 else
           r"D:\UE58Projects\ZeroAnim\Saved\PBZTraceWork\Session-20261125-055942-540\PBZ.log")


def printable_ratio(b: bytes) -> float:
    if not b:
        return 0.0
    ok = sum(1 for c in b if 32 <= c < 127 or c in (9, 10, 13))
    return ok / len(b)


def try_lz4_block(src: bytes, max_out: int = 1 << 20) -> bytes | None:
    out = bytearray()
    i = 0
    n = len(src)
    try:
        while i < n and len(out) < max_out:
            token = src[i]; i += 1
            lit = token >> 4
            if lit == 15:
                while True:
                    b = src[i]; i += 1; lit += b
                    if b != 255:
                        break
            out += src[i:i + lit]; i += lit
            if i >= n:
                break
            off = src[i] | (src[i + 1] << 8); i += 2
            if off == 0 or off > len(out):
                return None
            mlen = (token & 0x0F) + 4
            if (token & 0x0F) == 15:
                while True:
                    b = src[i]; i += 1; mlen += b
                    if b != 255:
                        break
            start = len(out) - off
            for k in range(mlen):
                out.append(out[start + k])
    except IndexError:
        return None
    return bytes(out)


def probe_compression(data: bytes) -> None:
    for name, off in (("offset8", 8), ("offset16", 16), ("offset0", 0)):
        body = data[off:]
        dec = try_lz4_block(body)
        if dec and len(dec) > 256:
            print(f"[LZ4 {name}] out={len(dec)} printable={printable_ratio(dec[:4000]):.2f}")
            print(dec[:300])
        for sig, label in ((b"\x28\xb5\x2f\xfd", "zstd"), (b"\x78\x9c", "zlib"), (b"\x78\xda", "zlibmax")):
            idx = body.find(sig)
            if 0 <= idx < 64:
                print(f"[{label} {name}] signature at +{idx}")


def probe_ciphers(data: bytes) -> None:
    from Crypto.Cipher import AES, ChaCha20

    head = data[:64]
    body = data[16:]
    keys = {
        "hex0d06": bytes.fromhex("0d068454d8c1dd2b83b0f8839d7f0604e0d28316c6036ec1213e0beaf290760d"),
        "hex8b5e": bytes.fromhex("8b5e54ca9aaf2e00435d39eb6aaa373e4f6db2ea134c59b0401be74e30458e55"),
        "magic_pad": (b"PBZLOGE1").ljust(32, b"\x00"),
    }
    nonces = {
        "zero12": b"\x00" * 12,
        "hdr8_12": head[8:20],
        "body0_12": body[:12],
        "hdr4_12": head[4:16],
    }
    ivs = {"zero16": b"\x00" * 16, "body0_16": body[:16], "hdr0_16": head[:16]}
    best = []
    for kn, key in keys.items():
        for nn, nonce in nonces.items():
            try:
                out = ChaCha20.new(key=key, nonce=nonce).decrypt(body[:4096])
                best.append((printable_ratio(out), f"chacha20 key={kn} nonce={nn}", out[:120]))
            except Exception:
                pass
        for ivn, iv in ivs.items():
            for mode in ("CTR", "CBC", "OFB", "CFB"):
                try:
                    c = getattr(AES, mode).new(key, iv)
                    out = c.decrypt(body[:4096])
                    best.append((printable_ratio(out), f"aes-{mode} key={kn} iv={ivn}", out[:120]))
                except Exception:
                    pass
    best.sort(reverse=True, key=lambda x: x[0])
    for ratio, label, sample in best[:8]:
        print(f"{ratio:.3f} {label}")
        print("      ", sample)


def main() -> int:
    data = LOG.read_bytes()
    print(f"file={LOG}\nsize={len(data)} magic={data[:8]!r}")
    print(f"header={data[:32].hex()}")
    print(f"entropy_head={printable_ratio(data[16:16+2048]):.3f} (printable ratio; ~0.4 => binary)")
    probe_compression(data)
    probe_ciphers(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
