#!/usr/bin/env python3
"""
Read a residual log dumped by krivine_rev --dump-residual and report
information-theoretic statistics about the residual stream.

File format ("RLOG0001"):
  bytes 0..7   magic "RLOG0001"
  bytes 8..15  int64_le count
  then `count` records of (tag:int32_le, addr:int64_le, old:int64_le).

For each program we report:
  raw_bytes          fixed 20 * count, on-disk size of the dump's record body
  excl_input_bytes   raw_bytes minus TAG_READ_INPUT entries (external input)
  H(tag)             zeroth-order entropy of tag column, bits per entry
  H(addr_delta)      same on addr-deltas (per-tag, then averaged by frequency)
  H(old_delta)       same on old-deltas
  H(joint)           H(tag, addr_delta, old_delta) joint zeroth-order
  gzip / zstd / xz   external compressor sizes on raw record body

External-input bytes (TAG_READ_INPUT) are reported separately so the
"can-we-compress-history" question is not contaminated by raw user input.
"""

import argparse
import gzip
import io
import lzma
import math
import os
import struct
import subprocess
import sys
from collections import Counter

# Mirror of rlog.h. Keep in sync.
TAG_NAMES = {
    1: "heap",
    2: "a",
    3: "C",
    4: "D",
    5: "U",
    6: "B",
    7: "c",
    8: "b",
    9: "m",
    10: "u",
    11: "H",
    12: "I",
    13: "O",
    14: "co",
    15: "END",
    16: "refcnt_dec",
    17: "freelist_push",
    18: "alloc_pop",
    19: "alloc_fresh",
    20: "read_input",
    21: "exit",
    22: "gro_expand",
    23: "put_bit",
    24: "put_byte",
}
TAG_READ_INPUT = 20

REC = struct.Struct("<iqq")  # tag i32, addr i64, old i64
REC_SIZE = REC.size  # 20


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"RLOG0001":
        raise SystemExit(f"{path}: bad magic")
    (count,) = struct.unpack("<q", data[8:16])
    body = data[16:]
    if len(body) != count * REC_SIZE:
        raise SystemExit(f"{path}: body size mismatch")
    records = [REC.unpack_from(body, i * REC_SIZE) for i in range(count)]
    return body, records


def shannon(counter):
    n = sum(counter.values())
    if n == 0:
        return 0.0
    h = 0.0
    for c in counter.values():
        p = c / n
        h -= p * math.log2(p)
    return h


def delta_records(records):
    """Recompute each entry's addr/old as the delta from the previous entry
    of the same tag. Equivalent to a coder that maintains per-tag last-value
    state. Returns a list of (tag, addr_delta, old_delta) triples in original
    order. The first occurrence of each tag uses the absolute value as delta."""
    last = {}
    out = []
    for tag, addr, old in records:
        prev_a, prev_o = last.get(tag, (0, 0))
        out.append((tag, addr - prev_a, old - prev_o))
        last[tag] = (addr, old)
    return out


def conditional_entropy_per_tag(deltas, key_idx):
    """H(X | tag) = sum_t p(t) H(X | tag=t), where X is column key_idx of
    (tag, addr_delta, old_delta)."""
    by_tag = {}
    for d in deltas:
        by_tag.setdefault(d[0], []).append(d[key_idx])
    total = sum(len(v) for v in by_tag.values())
    if total == 0:
        return 0.0
    h = 0.0
    for tag, seq in by_tag.items():
        h += (len(seq) / total) * shannon(Counter(seq))
    return h


def joint_entropy_triple(deltas):
    return shannon(Counter(deltas))


class ArithCoder:
    """Textbook bit-level arithmetic coder with E1/E2/E3 renormalization.
    32-bit precision; total of any frequency model must stay < 2**29 so the
    rng*cum products do not overflow before the // total step."""

    PRECISION = 32
    TOP = 1 << PRECISION
    HALF = TOP >> 1
    QUARTER = TOP >> 2
    THREE_Q = HALF + QUARTER

    def __init__(self):
        self.low = 0
        self.high = self.TOP - 1
        self.bits = bytearray()
        self.pending = 0

    def _emit(self, bit):
        self.bits.append(bit)
        for _ in range(self.pending):
            self.bits.append(1 - bit)
        self.pending = 0

    def encode(self, cum_lo, cum_hi, total):
        rng = self.high - self.low + 1
        self.high = self.low + (rng * cum_hi) // total - 1
        self.low = self.low + (rng * cum_lo) // total
        while True:
            if self.high < self.HALF:
                self._emit(0)
            elif self.low >= self.HALF:
                self._emit(1)
                self.low -= self.HALF
                self.high -= self.HALF
            elif self.low >= self.QUARTER and self.high < self.THREE_Q:
                self.pending += 1
                self.low -= self.QUARTER
                self.high -= self.QUARTER
            else:
                break
            self.low <<= 1
            self.high = (self.high << 1) | 1

    def finish(self):
        self.pending += 1
        self._emit(0 if self.low < self.QUARTER else 1)
        n = (len(self.bits) + 7) // 8
        out = bytearray(n)
        for i, b in enumerate(self.bits):
            if b:
                out[i >> 3] |= 1 << (7 - (i & 7))
        return bytes(out)


def varint_encode(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    out.append(n & 0x7F)
    return bytes(out)


def zigzag(n):
    return (n << 1) if n >= 0 else ((-n) << 1) - 1


def encode_log_adaptive(records):
    """Arithmetic-code the residual log over the joint-delta alphabet using
    online frequency adaptation (Method A): start with a single ESCAPE
    symbol of count 1; encode seen-before symbols against the current
    counts; on a new symbol, encode ESCAPE then transmit (tag, zigzag(ad),
    zigzag(od)) as varints with each byte uniformly distributed over [0,
    256). Then add the new symbol with count 1.

    No header. Returns (total_bytes, 0, payload_bytes) so the column lines
    up with the static encoder."""
    deltas = delta_records(records)
    ESCAPE = "__ESC__"
    freq = {ESCAPE: 1}
    order = [ESCAPE]
    coder = ArithCoder()
    for d in deltas:
        total = sum(freq.values())
        if d in freq:
            cum_lo = 0
            for s in order:
                if s == d:
                    break
                cum_lo += freq[s]
            coder.encode(cum_lo, cum_lo + freq[d], total)
            freq[d] += 1
        else:
            coder.encode(0, 1, total)  # ESCAPE always at index 0
            tag, ad, od = d
            for v in (tag, zigzag(ad), zigzag(od)):
                for byte in varint_encode(v):
                    coder.encode(byte, byte + 1, 256)
            freq[d] = 1
            order.append(d)
    payload = coder.finish()
    return len(payload), 0, len(payload)


def encode_log(records):
    """Arithmetic-code the residual log over the joint-delta alphabet
    (tag, addr_delta_within_tag, old_delta_within_tag).
    Returns (total_bytes, header_bytes, payload_bytes)."""
    deltas = delta_records(records)
    counts = Counter(deltas)
    syms = sorted(counts.keys())
    cum, c = {}, 0
    for s in syms:
        cum[s] = c
        c += counts[s]
    total = c

    header = bytearray(b"ARITH001")
    header += varint_encode(len(records))
    header += varint_encode(len(syms))
    for tag, ad, od in syms:
        header += varint_encode(tag)
        header += varint_encode(zigzag(ad))
        header += varint_encode(zigzag(od))
        header += varint_encode(counts[(tag, ad, od)])

    coder = ArithCoder()
    for d in deltas:
        coder.encode(cum[d], cum[d] + counts[d], total)
    payload = coder.finish()

    return len(header) + len(payload), len(header), len(payload)


def compressed_size(body, tool):
    if tool == "gzip":
        return len(gzip.compress(body, compresslevel=9))
    if tool == "xz":
        return len(lzma.compress(body, preset=9 | lzma.PRESET_EXTREME))
    if tool == "zstd":
        try:
            r = subprocess.run(
                ["zstd", "--ultra", "-22", "-c", "-q"],
                input=body,
                capture_output=True,
                check=True,
            )
            return len(r.stdout)
        except (FileNotFoundError, subprocess.CalledProcessError):
            return None
    raise ValueError(tool)


def report(path):
    body, records = load(path)
    n = len(records)
    raw = n * REC_SIZE
    tag_counts = Counter(r[0] for r in records)
    input_count = tag_counts.get(TAG_READ_INPUT, 0)
    excl = (n - input_count) * REC_SIZE

    deltas = delta_records(records)
    H_tag = shannon(tag_counts)
    H_addr_g_tag = conditional_entropy_per_tag(deltas, 1)  # H(addr_delta | tag)
    H_old_g_tag = conditional_entropy_per_tag(deltas, 2)  # H(old_delta  | tag)
    H_chain_loose = H_tag + H_addr_g_tag + H_old_g_tag  # upper bound on the floor
    H_joint_delta = joint_entropy_triple(deltas)  # tight zeroth-order floor

    floor_per_entry_bits = H_joint_delta
    floor_bytes = floor_per_entry_bits * n / 8

    coded_total, coded_hdr, coded_pay = encode_log(records)
    adapt_total, _, adapt_pay = encode_log_adaptive(records)
    gz = compressed_size(body, "gzip")
    xz = compressed_size(body, "xz")
    zs = compressed_size(body, "zstd")

    print(f"{os.path.basename(path)}")
    print(f"  entries               : {n}")
    print(f"  raw_bytes             : {raw}")
    print(f"  excl_input_bytes      : {excl}   ({input_count} read_input entries)")
    print(f"  H(tag)            b/e : {H_tag:.3f}   ({len(tag_counts)} tags seen)")
    print(f"  H(addr_d | tag)   b/e : {H_addr_g_tag:.3f}")
    print(f"  H(old_d  | tag)   b/e : {H_old_g_tag:.3f}")
    print(f"  chain (loose ub)  b/e : {H_chain_loose:.3f}")
    print(
        f"  joint-delta floor b/e : {floor_per_entry_bits:.3f} -> "
        f"{floor_bytes:.0f} bytes  (ratio {floor_bytes / raw:.3f})"
    )
    if gz is not None:
        print(f"  gzip -9               : {gz} bytes  (ratio {gz / raw:.3f})")
    if zs is not None:
        print(f"  zstd --ultra -22      : {zs} bytes  (ratio {zs / raw:.3f})")
    if xz is not None:
        print(f"  xz -9e                : {xz} bytes  (ratio {xz / raw:.3f})")
    print(
        f"  custom static         : {coded_total} bytes "
        f"(hdr {coded_hdr}, pay {coded_pay}, ratio {coded_total / raw:.3f}, "
        f"vs floor {coded_total / max(floor_bytes, 1):.2f}x)"
    )
    print(
        f"  custom adaptive       : {adapt_total} bytes "
        f"(no hdr, ratio {adapt_total / raw:.3f}, "
        f"vs floor {adapt_total / max(floor_bytes, 1):.2f}x)"
    )
    print(f"  tag histogram:")
    for tag, c in sorted(tag_counts.items(), key=lambda kv: -kv[1]):
        name = TAG_NAMES.get(tag, f"?{tag}")
        print(f"    {name:<14} {c:>8}  ({100 * c / n:5.1f}%)")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()
    for p in args.paths:
        report(p)


if __name__ == "__main__":
    main()
