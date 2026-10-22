#!/usr/bin/env python3
"""Extract the original recovery components for a separate LK shim test."""
import argparse
import gzip
import hashlib
import pathlib
import struct

def align(n, page):
    return (n + page - 1) // page * page

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('recovery', type=pathlib.Path)
    parser.add_argument('outdir', type=pathlib.Path)
    args = parser.parse_args()
    data = args.recovery.read_bytes()
    if data[:8] != b'ANDROID!':
        raise SystemExit('not an Android boot image')
    kernel_len, kernel_addr, ramdisk_len, ramdisk_addr, _, _, tags_addr, pagesize, _, _ = struct.unpack_from('<10I', data, 8)
    if (kernel_addr, ramdisk_addr, tags_addr, pagesize) != (0x40080000, 0x51b00000, 0x47880000, 2048):
        raise SystemExit('unexpected recovery image memory layout')
    zdata = data[pagesize:pagesize + kernel_len]
    import zlib
    gz = zlib.decompressobj(31)
    image = gz.decompress(zdata) + gz.flush()
    dtb = gz.unused_data
    if image[0x38:0x3c] != b'ARM\x64' or not dtb.startswith(b'\xd0\x0d\xfe\xed'):
        raise SystemExit('unexpected ARM64 Image / appended FDT')
    if len(image) % 8:
        raise SystemExit('Image length must be divisible by eight')
    ramdisk_start = pagesize + align(kernel_len, pagesize)
    ramdisk = data[ramdisk_start:ramdisk_start + ramdisk_len]
    if len(ramdisk) != ramdisk_len or not ramdisk.startswith(b'\x1f\x8b'):
        raise SystemExit('invalid recovery ramdisk')
    args.outdir.mkdir(parents=True, exist_ok=True)
    for name, content in [('recovery-Image', image), ('recovery-dtb', dtb), ('recovery-ramdisk.gz', ramdisk)]:
        (args.outdir / name).write_bytes(content)
        print(name, len(content), hashlib.sha256(content).hexdigest())
    print('Original recovery', hashlib.sha256(data).hexdigest())

if __name__ == '__main__':
    main()
