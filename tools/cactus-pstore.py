#!/usr/bin/env python3
"""Decode the CACTUS shim trace from a raw pstore dump.

  cactus-pstore.py DUMP.bin [OUT.txt]

DUMP.bin must start at the pstore base (0x47c90000).  Grab it with
tools/cactus-pstore-pull.sh from TWRP/rooted Android, or with:

  adb exec-out "dd if=/dev/mem bs=4096 skip=$((0x47c90000/4096)) \
      count=$((0xe0000/4096))" > pstore.bin

Header layout (shim/debug.h): magic, version, seq, count, entry/last ticks,
cpu, flags, last name, a/b/c, reserved.  Entries are 56 bytes at base+0x1000.
"""
import argparse
import struct
import sys

MAGIC = 0x31424443
TRACE_OFF = 0x1000
ENTRY_SIZE = 56
HEADER_SIZE = 88


def name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0,
                    help="byte offset of the pstore base inside DUMP")
    ap.add_argument("--freq", type=int, default=0,
                    help="counter frequency for converted timestamps")
    args = ap.parse_args()

    data = open(args.dump, "rb").read()
    off = args.base
    if len(data) < off + HEADER_SIZE:
        sys.exit("dump is too short")
    (magic, version, seq, count, entry_ticks, last_ticks, cpu, flags,
     last, a, b, c, res0, res1) = struct.unpack_from(
        "<IIIIQQII16sQQQII", data, off)
    if magic != MAGIC:
        sys.exit(f"no CACTUS trace at offset {off:#x} "
                 f"(magic {magic:#x}, want {MAGIC:#x})")

    def tick(v):
        if not args.freq:
            return str(v)
        return f"{v} ({v / args.freq:.6f}s)"

    lines = [
        f"# CACTUS pstore trace: version {version} seq {seq} count {count}",
        f"# cpu {cpu:#x} flags {flags:#x} res {res0:#x}/{res1:#x}",
        f"# first {tick(entry_ticks)} last {tick(last_ticks)}",
        f"# last event {name(last)!r} a={a:#x} b={b:#x} c={c:#x}",
    ]

    if count:
        first = seq - count + 1 if seq >= count else 1
        for s in range(first, seq + 1):
            pos = off + TRACE_OFF + ((s - 1) % max(count, 1)) * ENTRY_SIZE
            if pos + ENTRY_SIZE > len(data):
                lines.append(f"# entry {s}: truncated dump")
                break
            eseq, ecpu, ticks, ename, ea, eb, ec = struct.unpack_from(
                "<IIQ16sQQQ", data, pos)
            if eseq != s:
                lines.append(f"# entry {s}: bad seq {eseq} at {pos:#x}")
                continue
            lines.append(
                f"{s:6d} {ticks:16d} cpu{ecpu & 0xff:02x} "
                f"{name(ename):16s} a={ea:#018x} b={eb:#018x} c={ec:#018x}")

    text = "\n".join(lines) + "\n"
    if args.out:
        open(args.out, "w", newline="\n").write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
