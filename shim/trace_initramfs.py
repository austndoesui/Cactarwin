#!/usr/bin/env python3
"""Append a writable CPIO trace capsule to the original TWRP initramfs.

The first archive remains byte-for-byte the stock gzip stream. Linux's
initramfs parser accepts multiple concatenated (compressed or raw) archives.
The bare-metal stage probe fills one file in the second archive *in RAM*;
after Linux unpacks the ramdisk, ADB can read /cactus-trace.bin.

This is a stage-return diagnostic. It does not preserve XNU traces across
an unresponsive boot, a hard reset, or a later boot of a different image.
"""

import argparse
import hashlib
from pathlib import Path
import struct
import zlib


RAMDISK_ADDRESS = 0x51B00000
TRACE_LENGTH = 64 + 0x5000
TRACE_NAME = b"cactus-trace.bin\0"
RECORD = struct.Struct("<4sIIIQQQQIIQ")
assert RECORD.size == 64


def align4(buf: bytes) -> bytes:
    return buf + b"\0" * (-len(buf) % 4)


def cpio_file(name: bytes, data: bytes, inode: int) -> tuple[bytes, int]:
    # Linux initramfs newc: 6-byte ASCII signature + 13 eight-digit hex fields.
    fields = (
        inode, 0o100600 if data else 0, 0, 0, 1, 0, len(data),
        0, 0, 0, 0, len(name), 0,
    )
    header = b"070701" + b"".join(f"{v:08x}".encode("ascii") for v in fields)
    assert len(header) == 110
    prefix = align4(header + name)
    return prefix + align4(data), len(prefix)


def build(source: Path, output: Path, address_file: Path) -> None:
    stock = source.read_bytes()
    if stock[:2] != b"\x1f\x8b":
        raise SystemExit("original recovery ramdisk is not gzip")
    decompressor = zlib.decompressobj(31)
    decompressor.decompress(stock)
    decompressor.flush()
    if not decompressor.eof or decompressor.unused_data:
        raise SystemExit("original gzip stream is incomplete or has extra bytes")

    data = RECORD.pack(b"CTRC", 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    data += bytes(TRACE_LENGTH - len(data))
    first, value_offset = cpio_file(TRACE_NAME, data, 1)
    trailer, _ = cpio_file(b"TRAILER!!!\0", b"", 2)
    ramdisk = stock + first + trailer
    if ramdisk[:len(stock)] != stock:
        raise AssertionError("stock ramdisk was modified")
    offset = len(stock) + value_offset
    if ramdisk[offset:offset + 4] != b"CTRC":
        raise AssertionError("CPIO value offset is incorrect")
    # Validate exactly where Linux's gzip parser will resume processing.
    verify = zlib.decompressobj(31)
    verify.decompress(ramdisk)
    verify.flush()
    if not verify.eof or verify.unused_data != first + trailer:
        raise AssertionError("concatenated archive boundary is invalid")
    if not ramdisk[len(stock):].startswith(b"070701"):
        raise AssertionError("second initramfs archive is not raw newc")

    output.parent.mkdir(parents=True, exist_ok=True)
    address_file.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(ramdisk)
    address_file.write_text(f"0x{RAMDISK_ADDRESS + offset:x}\n", encoding="ascii")
    print(f"stock gzip       {len(stock)} bytes, sha256={hashlib.sha256(stock).hexdigest()}")
    print(f"extra CPIO       {len(first + trailer)} bytes")
    print(f"trace file       /{TRACE_NAME[:-1].decode('ascii')} ({len(data)} bytes)")
    print(f"trace offset     {offset:#x}")
    print(f"trace physical   {RAMDISK_ADDRESS + offset:#x} (requires LK's declared load address)")
    print(f"output           {output} ({len(ramdisk)} bytes)")


def decode(source: Path, extraction: Path | None) -> None:
    raw = source.read_bytes()
    if len(raw) != TRACE_LENGTH:
        raise SystemExit(f"expected {TRACE_LENGTH} bytes from /cactus-trace.bin, got {len(raw)}")
    magic, version, stage, status, dtb, el, mpidr, freq, wdt_mode, wdt_status, _ = RECORD.unpack_from(raw)
    print(f"magic       {magic!r} ({'shim wrote the trace' if magic == b'CTOK' else 'UNMODIFIED / INCOMPLETE'})")
    print(f"version     {version}   stage={stage}   status={status}")
    print(f"DTB x0      {dtb:#x}   CurrentEL={el:#x}   MPIDR={mpidr:#x}")
    print(f"timer       {freq} Hz   WDT_MODE={wdt_mode:#x}   WDT_STATUS={wdt_status:#x}")
    debug = raw[64:]
    print(f"CDB1 region {debug[:4]!r} ({len(debug)} bytes)")
    if stage == 7:
        values = struct.unpack_from("<16I", debug)
        keys = (
            "magic", "core_phys", "hwvers", "power", "devctl",
            "epinfo", "raminfo", "l1intm", "phy_rev_vbus",
            "phy_uart_dpdm", "phy_device_mode", "phy_gpio",
            "phy_trim", "intrusbe", "phy_regs_phys", "end",
        )
        if values[0] != 0x37425355 or values[-1] != 0x37444E45:
            raise SystemExit("USB7 record is missing/incomplete")
        print("MT6765 MUSB / USB2 PHY snapshot (read-only):")
        for key, value in zip(keys, values):
            print(f"  {key:17} 0x{value:08x}")
        print("Register visibility does not prove a USB gadget will enumerate.")
    if stage in (8, 9, 10, 11, 12, 13):
        fields = struct.unpack_from("<16I", debug)
        if fields[0] != 0x38425355 or fields[15] != 0x38444E45:
            raise SystemExit("USB8 proof record is incomplete")
        labels = (
            "magic", "hwvers", "power_before", "devctl_before",
            "usb_resets", "usb_addresses", "setup_packets", "configured",
            "dtr_rts", "last_setup", "hello_sent", "stalls", "rx_bytes",
            "state", "config_at_end", "end",
        )
        print(f"MUSB test Stage {stage}:")
        for label, value in zip(labels, fields):
            print(f"  {label:17} 0x{value:08x}")
        if stage == 9 and fields[13] == 9:
            print("C ABI + USB MMIO reads passed without USB writes.")
        elif stage == 10 and fields[13] == 10:
            print("One-second soft-connect experiment returned to TWRP.")
        elif stage == 11 and fields[13] == 11:
            print("Two-second IRQ+CSR0 sample completed; Stage 11 counters:")
            print(f"  resets={fields[4]} ep0_tx_irq={fields[5]} "
                  f"rx_ready_samples={fields[6]} iterations={fields[11]}")
            print(f"  last_usb_irq={fields[8]:#x} last_csr0={fields[9]:#x} "
                  f"last_devctl={fields[10]:#x} power_before_disconnect={fields[14]:#x}")
            print("Stage 11 does not ACK setup requests or enumerate.")
        elif stage == 12 and fields[13] == 12:
            print("Stage 12 CDC gadget returned after its 10-second USB window.")
            print(f"  resets={fields[4]} address_events={fields[5]} "
                  f"setups={fields[6]} configured={fields[7]} "
                  f"raw_frames_queued={fields[10]} stalls_or_bad_rx={fields[11]}")
            print("The raw frame is proven transferred only if the Windows")
            print("capture matches the expected 32-byte CLOG payload.")
        elif stage == 13 and fields[13] == 13:
            print("Stage 13 MT6765 PHY recovery + two-second USB sampling:")
            print(f"  usb_resets={fields[4]} ep0_tx_irq={fields[5]} "
                  f"rx_ready_samples={fields[6]} iterations={fields[11]}")
            print(f"  PHY +0x6c before={fields[7]:#x} active={fields[8]:#x}")
            print(f"  PHY +0x18 before={fields[12]:#x} active={fields[14]:#x}")
            print(f"  last_usb_irq={fields[9]:#x} last_devctl={fields[10]:#x}")
            print("This stage restores PHY settings and does not ACK USB setup.")
        elif fields[13] == 1:
            print("MUSB core identity check failed; no USB writes attempted.")
        elif fields[6] == 0:
            print("No host setup packets received; investigate PHY, clock, VBUS.")
        elif fields[7] == 0:
            print("Setup packets arrived, but USB SET_CONFIGURATION never completed.")
        else:
            print("USB SET_CONFIGURATION received; check Windows COM port.")
    if extraction is not None:
        extraction.write_bytes(debug)
        print(f"extracted   {extraction}")
    if magic != b"CTOK" or version != 1 or status != 1:
        raise SystemExit("the runtime marker was not completed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    prep = sub.add_parser("prepare", help="create appended ramdisk and trace address for stage 3/4")
    prep.add_argument("stock_gzip", type=Path)
    prep.add_argument("output", type=Path)
    prep.add_argument("address_file", type=Path)
    read = sub.add_parser("decode", help="decode ADB-pulled /cactus-trace.bin")
    read.add_argument("trace", type=Path)
    read.add_argument("--extract-cdb", type=Path, help="save raw CDB1 region for tools/cactus-pstore.py")
    args = parser.parse_args()
    if args.mode == "prepare":
        build(args.stock_gzip, args.output, args.address_file)
    else:
        decode(args.trace, args.extract_cdb)


if __name__ == "__main__":
    main()
