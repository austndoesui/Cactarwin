#!/usr/bin/env python3
"""Print the fields that matter for a cactus boot/recovery image."""
import hashlib
import math
import struct
import sys


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


def main(path):
    b = open(path, "rb").read()
    if len(b) < 4096:
        sys.exit("image too small")

    ks, ka = u32(b, 8), u32(b, 12)
    rs, ra = u32(b, 16), u32(b, 20)
    ss, sa = u32(b, 24), u32(b, 28)
    tags, ps = u32(b, 32), u32(b, 36)
    hv, osv = u32(b, 40), u32(b, 44)
    name = b[48:64].split(b"\0")[0].decode(errors="replace")
    cmdline = b[64:576].split(b"\0")[0].decode(errors="replace")
    rdtbo_size, rdtbo_off = u32(b, 1632), u64(b, 1636)
    header_size = u32(b, 1644)
    dtb_size, dtb_addr = u32(b, 1648), u64(b, 1652)

    print(f"file        {path}")
    print(f"size        {len(b)} sha256={hashlib.sha256(b).hexdigest()}")
    print(f"header      version={hv} size={header_size} page={ps}")
    print(f"kernel      size={ks:#x} addr={ka:#x}")
    print(f"ramdisk     size={rs:#x} addr={ra:#x}")
    print(f"second      size={ss:#x} addr={sa:#x}")
    print(f"tags        {tags:#x}")
    print(f"dtb         size={dtb_size:#x} addr={dtb_addr:#x}")
    print(f"recovery    dtbo_size={rdtbo_size:#x} off={rdtbo_off:#x}")
    print(f"cmdline     {cmdline}")
    print(f"name        {name!r}")

    if ps not in (2048, 4096, 8192, 16384):
        sys.exit("bad page size")
    ko = ps
    ro = ps + math.ceil(ks / ps) * ps
    so = ro + math.ceil(rs / ps) * ps
    rdo = so + math.ceil(ss / ps) * ps
    dto = rdo + math.ceil(rdtbo_size / ps) * ps

    kernel = b[ko:ko + ks]
    print(f"kernel@     {ko:#x} first4={kernel[:4].hex()}")
    fdt = kernel.find(b"\xd0\x0d\xfe\xed")
    if fdt >= 0 and fdt + 40 <= len(kernel):
        total = struct.unpack_from(">I", kernel, fdt + 4)[0]
        print(f"kernel FDT  off={fdt:#x} size={total} end={fdt + total:#x}")
    else:
        print("kernel FDT  not found")

    ramdisk = b[ro:ro + rs]
    if ramdisk[:2] == b"\x1f\x8b":
        rmagic = "gzip"
    elif ramdisk[:6] in (b"070701", b"070702"):
        rmagic = "cpio"
    elif ramdisk[:8] == b"SWEETXNU":
        rmagic = "SWEETXNU payload"
    else:
        rmagic = ramdisk[:8].hex()
    print(f"ramdisk@    {ro:#x} magic={rmagic}")

    if hv >= 2 and dtb_size:
        dtb = b[dto:dto + dtb_size]
        magic = struct.unpack_from(">I", dtb, 0)[0] if len(dtb) >= 4 else 0
        print(f"dtb@        {dto:#x} magic={magic:#010x} size={dtb_size}")
    else:
        print("dtb@        none (v1/appended)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} IMAGE")
    main(sys.argv[1])
