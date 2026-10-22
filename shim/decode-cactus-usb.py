#!/usr/bin/env python3
"""Decode 56-byte CLOG v2 frames from a possibly partial Windows COM capture.

No dependency beyond Python 3. This parser tolerates arbitrary read
boundaries, junk, truncated frames and lost/disconnected sequences.
"""
import argparse
import collections
from pathlib import Path
import struct
import zlib

MAGIC = b"CLOG"
SIZE = 56
BODY = struct.Struct("<4sBBBBIIIQQQQI")
assert BODY.size == SIZE


def decode(raw: bytes):
    records = []
    corrupt = 0
    noise = 0
    offset = 0
    while offset + SIZE <= len(raw):
        index = raw.find(MAGIC, offset)
        if index < 0:
            break
        noise += index - offset
        if index + SIZE > len(raw):
            offset = index
            break
        frame = raw[index:index + SIZE]
        fields = BODY.unpack(frame)
        (magic, version, size, stage, el, seq, session, event,
         ticks, arg0, arg1, arg2, crc) = fields
        if version != 2 or size != SIZE or zlib.crc32(frame[:52]) != crc:
            corrupt += 1
            offset = index + 1
            continue
        records.append(dict(offset=index, stage=stage, el=el,
                            seq=seq, session=session, event=event,
                            ticks=ticks, a=arg0, b=arg1, c=arg2))
        offset = index + SIZE
    noise += len(raw) - offset
    return records, corrupt, noise


def assess(records):
    seen = set()
    duplicates = 0
    gaps = 0
    regressions = 0
    last = {}
    for rec in records:
        key = rec["session"], rec["seq"]
        if key in seen:
            duplicates += 1
            continue
        seen.add(key)
        previous = last.get(rec["session"])
        if previous is not None:
            if rec["seq"] > previous + 1:
                gaps += rec["seq"] - previous - 1
            elif rec["seq"] <= previous:
                regressions += 1
        last[rec["session"]] = rec["seq"]
    return gaps, duplicates, regressions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--require-stage14", action="store_true",
                        help="fail if no useful Stage 14 hardware evidence")
    parser.add_argument("--require-stage15", action="store_true",
                        help="require Stage 15 shim entry, FDT, and layout checkpoints")
    parser.add_argument("--require-stage15-early", action="store_true",
                        help="require early real-shim USB entry and stream checkpoints")
    parser.add_argument("--require-stage16", action="store_true",
                        help="require Stage 16 real shim FDT/debug checkpoints and DONE")
    parser.add_argument("--require-stage17", action="store_true",
                        help="require Stage 17 live FDT memory map and completion")
    parser.add_argument("--require-stage18", action="store_true",
                        help="require real payload admission, layout READY and DONE")
    parser.add_argument("--early-level", type=int, choices=[1, 2, 3, 4], default=1,
                        help="expected Stage 15 early diagnostic level")
    parser.add_argument("--show", type=int, default=15)
    args = parser.parse_args()
    raw = args.capture.read_bytes()
    records, corrupt, noise = decode(raw)
    gaps, duplicates, regressions = assess(records)
    print(f"input={len(raw)} bytes valid_frames={len(records)} "
          f"bad_frames={corrupt} unmatched_bytes={noise}")
    print(f"gaps={gaps} duplicates={duplicates} regressions={regressions} "
          f"sessions={[hex(s) for s in sorted({r['session'] for r in records})]}")
    counts = collections.Counter(r["event"] for r in records)
    print("events", {hex(e): n for e, n in sorted(counts.items())})
    for r in records[:max(0, args.show)]:
        print(f"seq={r['seq']:4d} stage={r['stage']} EL={r['el']:#x} "
              f"event={r['event']:#x} tick={r['ticks']} "
              f"a={r['a']:#x} b={r['b']:#x} c={r['c']:#x}")
    # Deliberately demand a *changing* event stream, not a repeated
    # 32-byte Stage 12 marker, with consistent order and CRC.
    unique_sequences = len({(r["session"], r["seq"]) for r in records})
    valid = (len(records) >= 20 and unique_sequences >= 20 and
             any(r["event"] == 0x1403 for r in records) and
             all(r["stage"] == 14 for r in records) and
             not duplicates and not regressions and not corrupt)
    if args.require_stage14:
        print("STAGE14 " + ("PASS" if valid else "NOT VERIFIED"))
        if not valid:
            raise SystemExit(1)
    if args.require_stage15:
        events = {r["event"] for r in records if r["stage"] == 15}
        needed = {0x1500, 0x1501, 0x1502, 0x1503, 0x1504, 0x15ff}
        passed = (needed <= events and not corrupt and
                  not duplicates and not regressions and
                  all(r["stage"] == 15 for r in records))
        print("STAGE15 " + ("PREFLIGHT PASS" if passed else
                            f"NOT VERIFIED; missing={sorted(needed - events)}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage15_early:
        event_set = {r["event"] for r in records if r["stage"] == 15}
        required = {0x1580, 0x1581, 0x1582, 0x158a}
        if args.early_level >= 2:
            required |= {0x1583, 0x1584}
        if args.early_level >= 3:
            required |= {0x1585, 0x1586}
        if args.early_level >= 4:
            required |= {0x1587, 0x1588}
        done = any(r["event"] == 0x158a and r["a"] == args.early_level
                   for r in records)
        passed = (len(records) >= 12 and done and required <= event_set and
                  not corrupt and not duplicates and not regressions and
                  all(r["stage"] == 15 for r in records))
        print("STAGE15 EARLY " + ("PASS" if passed else
              f"NOT VERIFIED; missing={list(map(hex, sorted(required - event_set)))}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage16:
        stage_records = [r for r in records if r["stage"] == 16]
        required = (0x1600, 0x1601, 0x1610, 0x1611, 0x1612,
                    0x1620, 0x1621, 0x16ff)
        first = {}
        for index, record in enumerate(stage_records):
            first.setdefault(record["event"], index)
        all_present = all(event in first for event in required)
        ordered = all_present and all(first[x] < first[y]
                                      for x, y in zip(required, required[1:]))
        parse_ok = any(r["event"] == 0x1612 and r["a"] == 1
                       for r in stage_records)
        usb_ok = any(r["event"] == 0x1601 and r["a"] == 1
                     for r in stage_records)
        fault = any(r["event"] == 0x16ee for r in stage_records)
        passed = (ordered and parse_ok and usb_ok and not fault and
                  not corrupt and not duplicates and not regressions and
                  not gaps and len(stage_records) >= 8 and
                  len(stage_records) == len(records))
        print("STAGE16 " + ("PREFLIGHT PASS" if passed else
             "NOT VERIFIED; missing=" +
             str([hex(e) for e in required if e not in first]) +
             f" ordered={ordered} fdt_parsed={parse_ok} usb={usb_ok} fault={fault}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage17:
        selected = [r for r in records if r["stage"] == 17]
        needed = (0x1700, 0x1701, 0x1710, 0x1711,
                  0x1712, 0x1713, 0x1714, 0x1718, 0x17ff)
        first = {}
        for i, record in enumerate(selected):
            first.setdefault(record["event"], i)
        present = all(event in first for event in needed)
        ordered = present and all(first[a] < first[b]
                                  for a, b in zip(needed, needed[1:]))
        parsed = any(r["event"] == 0x1712 and r["a"] == 1
                     and r["b"] >= 1 for r in selected)
        header = any(r["event"] == 0x1711 and r["a"] == 1
                     for r in selected)
        connected = any(r["event"] == 0x1701 and r["a"] == 1
                        for r in selected)
        fault = any(r["event"] == 0x17ee for r in selected)
        passed = (ordered and parsed and header and connected and
                  not fault and not corrupt and not gaps and
                  not duplicates and not regressions and
                  len(selected) == len(records))
        print("STAGE17 " + ("MAP AND COMPLETION PASS" if passed else
              f"NOT VERIFIED; missing={[hex(e) for e in needed if e not in first]} "
              f"ordered={ordered} parsed={parsed} fault={fault}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage18:
        selected = [r for r in records if r["stage"] == 18]
        required = (0x1800, 0x1801, 0x1810, 0x1811,
                    0x1820, 0x1821, 0x1830, 0x1831,
                    0x1840, 0x18f0, 0x18ff)
        first = {}
        for i, r in enumerate(selected):
            first.setdefault(r["event"], i)
        present = all(e in first for e in required)
        ordered = present and all(first[a] < first[b]
                                  for a, b in zip(required, required[1:]))
        usb = any(r["event"] == 0x1801 and r["a"] == 1 for r in selected)
        fdt = any(r["event"] == 0x1811 and r["a"] == 1 for r in selected)
        ram = any(r["event"] == 0x1821 and r["a"] == 1 for r in selected)
        verdicts = [r["a"] for r in selected if r["event"] == 0x1831]
        done = any(r["event"] == 0x18ff and r["a"] == 0 for r in selected)
        ready = any(r["event"] == 0x18f0 and r["a"] and r["b"]
                    for r in selected)
        fault = any(r["event"] == 0x18ee for r in selected)
        segments = sum(r["event"] == 0x1840 for r in selected)
        claimed_segments = next((r["c"] for r in selected
                                 if r["event"] == 0x1820), 0)
        passed = (ordered and usb and fdt and ram and verdicts == [0]
                  and done and ready and not fault and
                  0 < claimed_segments <= 64 and segments == claimed_segments and
                  not corrupt and not gaps and not duplicates and
                  not regressions and len(selected) == len(records))
        print("STAGE18 " + ("PAYLOAD ADMISSION PASS" if passed else
              f"NOT VERIFIED; missing={[hex(e) for e in required if e not in first]} "
              f"ordered={ordered} usb={usb} fdt={fdt} ram={ram} "
              f"verdicts={verdicts} segments={segments}/{claimed_segments} "
              f"ready={ready} done={done} fault={fault}"))
        if not passed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
