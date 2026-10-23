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
    parser.add_argument("--require-stage20", action="store_true",
                        help="require 10 segments copied and EL1 ERET/HVC return")
    parser.add_argument("--require-stage20b", action="store_true",
                        help="require proof that XNU's first B reached its patched BRK target")
    parser.add_argument("--require-stage20c", action="store_true",
                        help="require early XNU framebuffer path and pre-VBAR breakpoint")
    parser.add_argument("--require-stage20d", action="store_true",
                        help="require real XNU VBAR handoff and post-VBAR EL1h exception")
    parser.add_argument("--require-stage21c1", action="store_true",
                        help="require pre-MMU EL1t trap, tables/sysregs and EL2 return")
    parser.add_argument("--require-stage21c2", action="store_true",
                        help="require post-MMU EL1t trap via translated vector and EL2 return")
    parser.add_argument("--require-stage21d", action="store_true",
                        help="require high-VA C checkpoint or captured exception and clean EL2 recovery")
    parser.add_argument("--require-stage22", action="store_true",
                        help="require Stage27 CPU/timer/memory snapshots and deeper XNU C checkpoint or captured fault")
    parser.add_argument("--require-stage23", action="store_true",
                        help="require Stage28 framebuffer preflight, native XNU entry, and genuine fault/held COM25 capture")
    parser.add_argument("--require-stage24", action="store_true",
                        help="require Stage29 native XNU exception plus pre-EL2-clobber register context")
    parser.add_argument("--require-stage25", action="store_true",
                        help="require Stage30 null-PTE checkpoint, exact VA/PA range and safe page-table descriptor report")
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
    if args.require_stage20:
        selected = [r for r in records if r["stage"] == 20]
        first = {}
        for i, r in enumerate(selected):
            first.setdefault(r["event"], i)
        required = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                    0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                    0x1952, 0x20a0, 0x20a1, 0x20a2, 0x20a3,
                    0x20ff, 0x19ff)
        present = all(e in first for e in required)
        ordered = present and all(first[a] < first[b]
                                  for a, b in zip(required, required[1:]))
        starts = [r["a"] for r in selected if r["event"] == 0x1940]
        finishes = [r["a"] for r in selected if r["event"] == 0x1941]
        ten = starts == list(range(10)) and finishes == list(range(10))
        fdt = any(r["event"] == 0x1910 and r["a"] == 1 for r in selected)
        accepted = any(r["event"] == 0x1911 and r["a"] == 0 for r in selected)
        usb = any(r["event"] == 0x1901 and r["a"] == 1 for r in selected)
        at_el1 = any(r["event"] == 0x20a1 and r["el"] == 4
                     and r["a"] == 4 and r["b"] == 0x8142d488
                     and r["c"] == 0x824bc000 for r in selected)
        hvc = any(r["event"] == 0x20a2 and r["el"] == 8
                  and (r["a"] >> 26) & 0x3f == 0x16
                  and (r["a"] & 0xffff) == 0x20
                  and (r["c"] & 0xf) == 5 for r in selected)
        returned = any(r["event"] == 0x20a3 and r["el"] == 8
                       and r["a"] == 8 for r in selected)
        done = all(any(r["event"] == e and r["a"] == 0 for r in selected)
                   for e in (0x20ff, 0x19ff))
        passed = (present and ordered and ten and usb and fdt and accepted
                  and at_el1 and hvc and returned and done
                  and not any(r["event"] == 0x19ee for r in selected)
                  and not corrupt and not noise and not gaps
                  and not duplicates and not regressions
                  and len(selected) == len(records))
        print("STAGE20 " + ("EL1 ERET/HVC RETURN PASS" if passed else
              f"NOT VERIFIED: present={present} ordered={ordered} "
              f"segments={len(starts)}/{len(finishes)} ten={ten} "
              f"EL1={at_el1} HVC={hvc} returned={returned} "
              f"done={done} usb={usb} fdt={fdt} accepted={accepted}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage20b:
        selected = [r for r in records if r["stage"] == 21]
        first = {}
        for index, item in enumerate(selected):
            first.setdefault(item["event"], index)
        expected = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930,
                    0x1940, 0x1941, 0x1950, 0x1951, 0x1952, 0x20b0,
                    0x20a0, 0x20a1, 0x20b2, 0x20b3, 0x20a3, 0x20b4,
                    0x20ff, 0x19ff)
        present = all(e in first for e in expected)
        ordered = present and all(first[a] < first[b]
                                  for a, b in zip(expected, expected[1:]))
        starts = [r["a"] for r in selected if r["event"] == 0x1940]
        finishes = [r["a"] for r in selected if r["event"] == 0x1941]
        ten = starts == list(range(10)) and finishes == list(range(10))
        usb = any(r["event"] == 0x1901 and r["a"] == 1 for r in selected)
        fdt = any(r["event"] == 0x1910 and r["a"] == 1 for r in selected)
        admitted = any(r["event"] == 0x1911 and r["a"] == 0 for r in selected)
        entry = any(r["event"] == 0x20b0 and r["a"] == 0x8142d488
                    and r["b"] == 0x8142f0ec and r["c"] == 0x14000719
                    and r["el"] == 8 for r in selected)
        el1 = any(r["event"] == 0x20a1 and r["el"] == 4
                  and r["a"] == 4 and r["b"] == 0x8142d488
                  and r["c"] == 0x824bc000 for r in selected)
        trap = any(r["event"] == 0x20b2 and r["el"] == 4
                   and r["a"] >> 26 == 0x3c
                   and r["a"] & 0xffff == 0xb20
                   and r["b"] == 0x8142f0ec for r in selected)
        hvc = any(r["event"] == 0x20b3 and r["el"] == 8
                  and r["a"] >> 26 == 0x16
                  and r["a"] & 0xffff == 0x21
                  and r["c"] & 0xf == 5 for r in selected)
        returned = any(r["event"] == 0x20a3 and r["el"] == 8
                       and r["a"] == 8 for r in selected)
        restored = any(r["event"] == 0x20b4 and
                       (r["a"], r["b"], r["c"]) == (1, 1, 1)
                       for r in selected)
        done = all(any(r["event"] == e and r["a"] == 0 for r in selected)
                   for e in (0x20ff, 0x19ff))
        passed = (ordered and ten and usb and fdt and admitted and entry
                  and el1 and trap and hvc and returned and restored and done
                  and not any(r["event"] == 0x19ee for r in selected)
                  and not (corrupt or noise or gaps or duplicates or regressions)
                  and len(selected) == len(records))
        print("STAGE20-B " + ("FIRST REAL XNU BRANCH PASS" if passed else
              f"NOT VERIFIED; missing={[hex(e) for e in expected if e not in first]} "
              f"ordered={ordered} segments={ten} usb={usb} fdt={fdt} "
              f"admitted={admitted} entry={entry} el1={el1} trap={trap} "
              f"hvc={hvc} returned={returned} restored={restored} done={done}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage20c:
        selected = [r for r in records if r["stage"] == 22]
        events = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930,
                  0x1940, 0x1941, 0x1950, 0x1951, 0x1952, 0x20c0,
                  0x20a0, 0x20a1, 0x20c1, 0x20c2, 0x20b3, 0x20a3,
                  0x20c3, 0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in events) and all(
            first[a] < first[b] for a, b in zip(events, events[1:]))
        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        connected = any(r["event"] == 0x1901 and r["a"] == 1 for r in selected)
        fdt = any(r["event"] == 0x1910 and r["a"] == 1 for r in selected)
        admitted = any(r["event"] == 0x1911 and r["a"] == 0 for r in selected)
        armed = any(r["event"] == 0x20c0 and r["el"] == 8
                    and (r["a"], r["b"], r["c"])
                    == (0x8142d488, 0x8142f144, 0x14000719)
                    for r in selected)
        enter = any(r["event"] == 0x20a1 and r["el"] == 4
                    and (r["a"], r["b"], r["c"])
                    == (4, 0x8142d488, 0x824bc000) for r in selected)
        breakpoint = any(r["event"] == 0x20c1 and r["el"] == 4
                         and (r["a"] >> 26) == 0x3c
                         and (r["a"] & 0xffff) == 0xc20
                         and r["b"] == 0x8142f144 for r in selected)
        fb = any(r["event"] == 0x20c2 and r["el"] == 4
                 and (r["a"], r["b"], r["c"])
                 == (0x824bc000, 0xffffffff, 0xffffffff)
                 for r in selected)
        hvc = any(r["event"] == 0x20b3 and r["el"] == 8
                  and (r["a"] >> 26) == 0x16 and
                  (r["a"] & 0xffff) == 0x21 and
                  (r["c"] & 0xf) == 5 for r in selected)
        el2 = any(r["event"] == 0x20a3 and r["el"] == 8 and r["a"] == 8
                  for r in selected)
        restored = any(r["event"] == 0x20c3 and
                       (r["a"], r["b"], r["c"]) == (1, 1, 1)
                       for r in selected)
        done = all(any(r["event"] == event and r["a"] == 0
                       for r in selected) for event in (0x20ff, 0x19ff))
        passed = (ordered and copies and connected and fdt and admitted
                  and armed and enter and breakpoint and fb and hvc and el2
                  and restored and done
                  and not any(r["event"] == 0x19ee for r in selected)
                  and not (corrupt or noise or gaps or duplicates or regressions)
                  and len(selected) == len(records))
        print("STAGE20-C " + ("EARLY XNU FRAMEBUFFER/VBAR GATE PASS"
              if passed else
              f"NOT VERIFIED ordered={ordered} copies={copies} "
              f"usb={connected} fdt={fdt} admitted={admitted} "
              f"armed={armed} EL1={enter} BRK={breakpoint} fb={fb} "
              f"hvc={hvc} EL2={el2} restore={restored} done={done}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage20d:
        selected = [r for r in records if r["stage"] == 23]
        expected = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911, 0x1930,
                    0x1940, 0x1941, 0x1950, 0x1951, 0x1952,
                    0x20d6, 0x20d0, 0x20a0, 0x20a1, 0x20d1,
                    0x20d2, 0x20d3, 0x20c2, 0x20b3, 0x20a3,
                    0x20d5, 0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in expected) and all(
            first[a] < first[b] for a, b in zip(expected, expected[1:]))
        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        connected = any(r["event"] == 0x1901 and r["a"] == 1 for r in selected)
        fdt = any(r["event"] == 0x1910 and r["a"] == 1 for r in selected)
        admitted = any(r["event"] == 0x1911 and r["a"] == 0 for r in selected)
        vector = any(r["event"] == 0x20d6 and r["el"] == 8
                     and r["a"] == 0x8142e000 and r["b"] == 0x8142e200
                     and r["c"] != 0 and r["c"] % 4 == 0
                     for r in selected)
        arm = any(r["event"] == 0x20d0 and r["el"] == 8
                  and (r["a"], r["b"], r["c"])
                  == (0x8142d488, 0x8142f154, 0x14000719)
                  for r in selected)
        entry = any(r["event"] == 0x20a1 and r["el"] == 4
                    and (r["a"], r["b"], r["c"])
                    == (4, 0x8142d488, 0x824bc000) for r in selected)
        trap = any(r["event"] == 0x20d1 and r["el"] == 4
                   and r["a"] >> 26 == 0x3c and r["a"] & 0xffff == 0xd20
                   and r["b"] == 0x8142f154 for r in selected)
        vbar = any(r["event"] == 0x20d2 and r["el"] == 4
                   and (r["a"], r["b"], r["c"])
                   == (0x8142e000, 0x824bc000, 0xfffffff006000000)
                   for r in selected)
        memory = any(r["event"] == 0x20d3 and r["el"] == 4
                     and (r["a"], r["b"], r["c"])
                     == (0x80000000, 0x1e000000, 0x1e000000)
                     for r in selected)
        pixels = any(r["event"] == 0x20c2 and r["el"] == 4
                     and (r["a"], r["b"], r["c"])
                     == (0x824bc000, 0xffffffff, 0xffffffff)
                     for r in selected)
        hvc = any(r["event"] == 0x20b3 and r["el"] == 8
                  and r["a"] >> 26 == 0x16 and r["a"] & 0xffff == 0x21
                  and r["c"] & 0xf == 5 for r in selected)
        returned = any(r["event"] == 0x20a3 and r["el"] == 8
                       and r["a"] == 8 for r in selected)
        restored = any(r["event"] == 0x20d5 and
                       (r["a"], r["b"], r["c"]) == (1, 1, 1)
                       for r in selected)
        done = all(any(r["event"] == event and r["a"] == 0
                       for r in selected) for event in (0x20ff, 0x19ff))
        passed = (ordered and copies and connected and fdt and admitted
                  and vector and arm and entry and trap and vbar and memory
                  and pixels and hvc and returned and restored and done
                  and not any(r["event"] == 0x19ee for r in selected)
                  and not (corrupt or noise or gaps or duplicates or regressions)
                  and len(selected) == len(records))
        print("STAGE20-D " + ("POST-VBAR EL1h VECTOR PASS" if passed else
              f"NOT VERIFIED ordered={ordered} copies={copies} "
              f"usb={connected} fdt={fdt} admission={admitted} "
              f"vector={vector} armed={arm} EL1={entry} trap={trap} "
              f"vbar={vbar} bootargs={memory} pixels={pixels} "
              f"hvc={hvc} returned={returned} restored={restored} done={done}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage25:
        selected = [r for r in records if r["stage"] == 30]
        events = {}
        for i, rec in enumerate(selected):
            events.setdefault(rec["event"], []).append((i, rec))

        def present(event, predicate=lambda r: True):
            return len(events.get(event, [])) == 1 and predicate(events[event][0][1])

        order = (0x1900, 0x1901, 0x2203, 0x2204, 0x2300,
                 0x2500, 0x20a0, 0x2200, 0x2201, 0x2202,
                 0x2301, 0x2302, 0x2303, 0x20a1, 0x2206,
                 0x2400, 0x2401, 0x2402, 0x2403, 0x2404,
                 0x2405, 0x2406, 0x2407, 0x2501, 0x2502,
                 0x2503, 0x2504, 0x2507, 0x241f, 0x2207,
                 0x2208, 0x2209, 0x220a, 0x20b3, 0x20a3,
                 0x2508, 0x220b, 0x2308, 0x20ff, 0x19ff)
        ordered = (all(e in events for e in order) and
                   all(events[a][0][0] < events[b][0][0]
                       for a, b in zip(order, order[1:])))
        clean = (len(selected) == len(records) and not
                 (corrupt or noise or gaps or duplicates or regressions))
        breakpoint = (present(0x2500, lambda r:
                      (r["a"], r["b"], r["c"]) ==
                      (0x81405218, 0xfffffff007405218, 0x941ae25e)) and
                      present(0x2206, lambda r:
                      r["a"] == 0xf2000e25 and r["b"] == 0xfffffff007405218) and
                      present(0x2501, lambda r:
                      (r["a"], r["b"], r["c"]) ==
                      (0xf2000e25, 0xfffffff007405218, 1)) and
                      present(0x220a, lambda r:
                      (r["a"], r["b"], r["c"]) ==
                      (3, 0xfffffff007405218, 0x3c)))
        # Exact mapping request must agree with original callee-saved regs.
        mapping = (present(0x2502) and present(0x2503) and
                   present(0x2404) and present(0x2405) and
                   events[0x2502][0][1]["a"] == events[0x2404][0][1]["a"] and
                   events[0x2502][0][1]["b"] == events[0x2405][0][1]["a"] and
                   events[0x2502][0][1]["c"] == events[0x2404][0][1]["c"] and
                   events[0x2503][0][1]["a"] == events[0x2404][0][1]["b"] and
                   events[0x2502][0][1]["a"] >= events[0x2503][0][1]["a"] and
                   events[0x2502][0][1]["b"] <= events[0x2502][0][1]["c"])
        walk = (present(0x2507, lambda r: 1 <= r["c"] <= 3) and
                present(0x2504) and present(0x2208) and
                events[0x2507][0][1]["a"] == events[0x2502][0][1]["a"] and
                events[0x2507][0][1]["b"] == events[0x2208][0][1]["b"] and
                all(present(0x2504 + level)
                    for level in range(events[0x2507][0][1]["c"])))
        original = (present(0x2407, lambda r: r["c"] == 30) and
                    present(0x2508, lambda r:
                    (r["a"], r["b"], r["c"]) ==
                    (1, 0x81405218, 0x941ae25e)) and
                    present(0x220b, lambda r:
                    (r["a"], r["b"], r["c"]) == (1, 1, 1)) and
                    present(0x2308, lambda r:
                    (r["a"], r["b"], r["c"]) == (0, 1, 1)) and
                    present(0x20ff, lambda r: r["a"] == 0) and
                    present(0x19ff, lambda r: r["a"] == 0))
        passed = ordered and clean and breakpoint and mapping and walk and original
        print("STAGE25 " + ("NULL-PTE REQUEST CAPTURED; COM25 HELD" if passed else
              f"NOT VERIFIED order={ordered} clean={clean} "
              f"breakpoint={breakpoint} mapping={mapping} "
              f"walk={walk} restored={original}"))
        if passed:
            fault = events[0x2502][0][1]
            state = events[0x2503][0][1]
            summary = events[0x2507][0][1]
            last = events[0x2504 + summary["c"] - 1][0][1]
            kinds = {0: "invalid descriptor", 1: "block descriptor",
                     2: "reserved descriptor", 3: "table/page descriptor"}
            print(f"STAGE25 failing_va={fault['a']:#018x} "
                  f"initial_va={state['a']:#018x} "
                  f"phys_start={fault['b']:#018x} "
                  f"phys_end={fault['c']:#018x} "
                  f"page_walk_depth={summary['c']} "
                  f"last_slot={last['a']:#018x} "
                  f"last_descriptor={last['b']:#018x} "
                  f"({kinds[last['b']&3]})")
        if not passed:
            raise SystemExit(1)
    if args.require_stage23 or args.require_stage24:
        selected = [r for r in records if r["stage"] ==
                    (29 if args.require_stage24 else 28)]
        prefix = "STAGE24" if args.require_stage24 else "STAGE23"
        by_event = {}
        for index, r in enumerate(selected):
            by_event.setdefault(r["event"], index)

        def seen(event, predicate):
            return any(r["event"] == event and predicate(r) for r in selected)

        first = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                 0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                 0x1952, 0x2203, 0x2204, 0x2300, 0x20a0,
                 0x2200, 0x2201, 0x2202, 0x2205, 0x2301,
                 0x2302, 0x2303, 0x20a1)
        ordered = all(e in by_event for e in first) and all(
            by_event[a] < by_event[b] for a, b in zip(first, first[1:]))
        clean = (len(selected) == len(records) and not
                 (corrupt or noise or gaps or duplicates or regressions) and
                 not seen(0x19ee, lambda r: True) and
                 not seen(0x23ee, lambda r: True))
        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        platform = (seen(0x1901, lambda r: r["a"] == 1) and
                    seen(0x1910, lambda r: r["a"] == 1) and
                    seen(0x1911, lambda r: r["a"] == 0) and
                    seen(0x2200, lambda r: (r["a"] >> 24) == 0x41 and
                         ((r["a"] >> 4) & 0xfff) == 0xd03 and r["c"] == 8) and
                    seen(0x2201, lambda r: r["a"] == 13000000) and
                    seen(0x2202, lambda r: r["a"] == 0x80000000 and
                         r["b"] == 0x1e000000))
        vectors = seen(0x2203, lambda r:
                       (r["a"], r["b"], r["c"]) ==
                       (0x81429000, 0x81429200, 0xfffffff007429000))
        original = seen(0x2300, lambda r:
                        (r["a"], r["b"], r["c"]) ==
                        (0x81ac0c0c, 0xb0ffacc1, 0xb0ffacc1))
        splash = seen(0x2301, lambda r:
                      r["a"] == 0x5e605000 + 80*2880 + 48*4 and
                      r["b"] == 0x00ff00ff0000ff00 and
                      r["c"] == 0x0000ffff) and seen(
                      0x2302, lambda r:
                      r["a"] == (48 << 32) | 80 and
                      r["b"] == (288 << 32) | 80 and r["c"] == 1)
        handoff = seen(0x2303, lambda r:
                       (r["a"], r["b"], r["c"]) ==
                       (0x8142d488, 0x824bc000, 0xfffffff007ac0c0c))
        el1 = seen(0x20a1, lambda r:
                   r["el"] == 4 and r["a"] == 4 and
                   r["b"] == 0x8142d488 and r["c"] == 0x824bc000)
        common = (clean and ordered and copies and platform and vectors and
                  original and splash and handoff and el1)
        if not common:
            print(f"{prefix} NOT VERIFIED clean={clean} order={ordered} "
                  f"copies={copies} cpu={platform} vectors={vectors} "
                  f"target_original={original} splash_readback={splash} "
                  f"handoff={handoff} el1={el1}")
            raise SystemExit(1)

        fault_order = ((0x2206,) +
                       ((0x2400, 0x2401, 0x2402, 0x2403, 0x2404,
                         0x2405, 0x2406, 0x2407, 0x241f)
                        if args.require_stage24
                        else ()) +
                       (0x2207, 0x2208, 0x2209, 0x220a,
                        0x20b3, 0x20a3, 0x220b, 0x2308, 0x20ff, 0x19ff))
        if not all(e in by_event for e in fault_order):
            print(f"{prefix} EL1 ENTRY OBSERVED; XNU OUTCOME UNKNOWN "
                  "(no complete synchronous-fault capture)")
            raise SystemExit(2)
        tail_ok = by_event[0x20a1] < by_event[0x2206] and all(
            by_event[a] < by_event[b] for a, b in zip(
                fault_order, fault_order[1:]))
        faults = [r for r in selected if r["event"] == 0x2206 and
                  (r["a"] >> 26) != 0x3c]
        fault = len(faults) == 1
        classification = (fault and seen(0x220a, lambda r:
                          r["a"] == 2 and r["b"] == faults[0]["b"] and
                          r["c"] == faults[0]["a"] >> 26))
        registers = (seen(0x2207, lambda r:
                     r["a"] == 0xfffffff007429000 and (r["c"] & 1)) and
                     seen(0x2209, lambda r:
                     r["a"] in (0xfffffff007429004, 0xfffffff007429204) and
                     (r["b"] & 15) == 5 and (r["c"] & 15) in (4, 5)))
        hvc = seen(0x20b3, lambda r: (r["a"] >> 26) == 0x16 and
                   (r["a"] & 0xffff) == 0x21)
        returned = seen(0x20a3, lambda r: r["el"] == 8 and r["a"] == 8)
        restored = seen(0x220b, lambda r:
                        (r["a"], r["b"], r["c"]) == (1, 1, 1))
        held = seen(0x2308, lambda r:
                    (r["a"], r["b"], r["c"]) == (0, 1, 1))
        completed = all(seen(e, lambda r: r["a"] == 0)
                        for e in (0x20ff, 0x19ff))
        register_dump = (not args.require_stage24 or (
            all(sum(r["event"] == e for r in selected) == 1
                for e in range(0x2400, 0x2408)) and
            seen(0x2400, lambda r: r["c"] != 0) and
            seen(0x2407, lambda r: r["c"] == 0x29) and
            seen(0x241f, lambda r: r["c"] <= 4 and any(
                v["event"] == 0x2208 and v["b"] == r["b"]
                for v in selected))))
        passed = (tail_ok and fault and classification and registers and
                  register_dump and hvc and returned and restored and held and
                  completed)
        print(f"{prefix} " + ("GENUINE XNU FAULT CAPTURED; COM25 HELD" if
              passed else f"NOT VERIFIED fault={fault} class={classification} "
              f"registers={registers} hvc={hvc} returned={returned} "
              f"restored={restored} held={held} done={completed} "
              f"context={register_dump}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage22:
        selected = [r for r in records if r["stage"] == 27]
        required = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                    0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                    0x1952, 0x2203, 0x2204, 0x20a0, 0x2200,
                    0x2201, 0x2202, 0x2205, 0x20a1, 0x2206,
                    0x2207, 0x2208, 0x2209, 0x220a, 0x20b3,
                    0x20a3, 0x220b, 0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in required) and all(
            first[a] < first[b] for a, b in zip(required, required[1:]))

        def seen(event, pred):
            return any(r["event"] == event and pred(r) for r in selected)

        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        admission = (seen(0x1901, lambda r: r["a"] == 1) and
                     seen(0x1910, lambda r: r["a"] == 1) and
                     seen(0x1911, lambda r: r["a"] == 0))
        cpu = seen(0x2200, lambda r:
                   ((r["a"] >> 24) & 0xff) == 0x41 and
                   ((r["a"] >> 4) & 0xfff) == 0xd03 and r["c"] == 8)
        timer = seen(0x2201, lambda r: r["a"] == 13000000 and
                     r["b"] > 0 and r["c"] > 0)
        memory = seen(0x2202, lambda r:
                      r["a"] == 0x80000000 and r["b"] == 0x1e000000 and
                      0x824bc000 <= r["c"] <= 0x9e000000)
        vectors = seen(0x2203, lambda r:
                       (r["a"], r["b"], r["c"]) ==
                       (0x81429000, 0x81429200, 0xfffffff007429000))
        armed = seen(0x2204, lambda r:
                     (r["a"], r["b"], r["c"]) ==
                     (0x8142d488, 0x81ac0c0c, 0x14000719))
        preflight = seen(0x2205, lambda r:
                         r["a"] == 0x81ac0c0c and
                         r["b"] == 0xfffffff007ac0c0c)
        el1 = seen(0x20a1, lambda r: r["el"] == 4 and
                   (r["a"], r["b"], r["c"]) ==
                   (4, 0x8142d488, 0x824bc000))
        mmu = seen(0x2207, lambda r:
                   r["a"] == 0xfffffff007429000 and (r["c"] & 1) == 1)
        origin = seen(0x2209, lambda r:
                      r["a"] in (0xfffffff007429004, 0xfffffff007429204) and
                      (r["b"] & 0xf) == 5 and (r["c"] & 0xf) in (4, 5))
        hvc = seen(0x20b3, lambda r: (r["a"] >> 26) == 0x16 and
                   (r["a"] & 0xffff) == 0x21 and
                   r["b"] in (0xfffffff007429004, 0xfffffff007429204))
        milestone = seen(0x2206, lambda r:
                         (r["a"] >> 26) == 0x3c and
                         (r["a"] & 0xffff) == 0xe24 and
                         r["b"] == 0xfffffff007ac0c0c)
        fault = seen(0x2206, lambda r:
                     (r["a"] >> 26) != 0x3c)
        other_brk = seen(0x2206, lambda r:
                         (r["a"] >> 26) == 0x3c and
                         ((r["a"] & 0xffff) != 0xe24 or
                          r["b"] != 0xfffffff007ac0c0c))
        kind = (1 if milestone else (2 if fault else 3 if other_brk else 0))
        classified = seen(0x220a, lambda r:
                          r["a"] == kind and any(
                              e["event"] == 0x2206 and e["b"] == r["b"] and
                              (e["a"] >> 26) == r["c"] for e in selected))
        returned = seen(0x20a3, lambda r: r["el"] == 8 and r["a"] == 8)
        restored = seen(0x220b, lambda r:
                        (r["a"], r["b"], r["c"]) == (1, 1, 1))
        finished = all(seen(e, lambda r: r["a"] == 0)
                       for e in (0x20ff, 0x19ff))
        valid = (ordered and copies and admission and cpu and timer and
                 memory and vectors and armed and preflight and el1 and
                 mmu and origin and hvc and kind and classified and
                 returned and restored and finished and
                 len(selected) == len(records) and
                 not (corrupt or noise or gaps or duplicates or regressions) and
                 not any(r["event"] == 0x19ee for r in selected))
        verdict = ("DEEPER XNU C CHECKPOINT PASS" if milestone else
                   "FIRST XNU EXCEPTION CAPTURED" if fault else
                   "XNU BRK CAPTURED" if other_brk else "NOT VERIFIED")
        print("STAGE22 " + (verdict if valid else
              f"NOT VERIFIED ordered={ordered} cpu={cpu} timer={timer} "
              f"memory={memory} vectors={vectors} armed={armed} "
              f"diagnostic={kind} HVC={hvc} restore={restored} done={finished}"))
        if not valid:
            raise SystemExit(1)
    if args.require_stage21d:
        selected = [r for r in records if r["stage"] == 26]
        required = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                    0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                    0x1952, 0x21e6, 0x21e0, 0x20a0, 0x21ea,
                    0x20a1, 0x21e1, 0x21e2, 0x21e3, 0x21e4,
                    0x21e5, 0x20b3, 0x20a3, 0x21e8, 0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in required) and all(
            first[a] < first[b] for a, b in zip(required, required[1:]))

        def match(event, predicate):
            return any(r["event"] == event and predicate(r) for r in selected)

        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        admission = (match(0x1901, lambda r: r["a"] == 1) and
                     match(0x1910, lambda r: r["a"] == 1) and
                     match(0x1911, lambda r: r["a"] == 0))
        vectors = match(0x21e6, lambda r: r["el"] == 8 and
                        (r["a"], r["b"], r["c"]) ==
                        (0x81429000, 0x81429200, 0xfffffff007429000))
        armed = match(0x21e0, lambda r: r["el"] == 8 and
                      (r["a"], r["b"], r["c"]) ==
                      (0x8142d488, 0x81ac061c, 0x14000719))
        preflight = match(0x21ea, lambda r: r["el"] == 8 and
                          r["a"] == 0x81ac061c and
                          r["b"] == 0xfffffff007ac061c)
        el1 = match(0x20a1, lambda r: r["el"] == 4 and
                    (r["a"], r["b"], r["c"]) ==
                    (4, 0x8142d488, 0x824bc000))
        mmu = match(0x21e2, lambda r: r["el"] == 8 and
                    r["a"] == 0xfffffff007429000 and
                    r["b"] == 0x22a5192519 and (r["c"] & 1) == 1)
        translation = match(0x21e3, lambda r: r["el"] == 8 and
                            (r["a"], r["b"], r["c"]) ==
                            (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff))
        origin = match(0x21e4, lambda r: r["el"] == 8 and
                       r["a"] in (0xfffffff007429004, 0xfffffff007429204) and
                       (r["b"] & 0xf) == 5 and (r["c"] & 0xf) in (4, 5))
        hvc = match(0x20b3, lambda r: r["el"] == 8 and
                    (r["a"] >> 26) == 0x16 and
                    (r["a"] & 0xffff) == 0x21 and
                    r["b"] in (0xfffffff007429004, 0xfffffff007429204) and
                    (r["c"] & 0xf) == 5)
        brk = match(0x21e1, lambda r: r["el"] == 8 and
                    (r["a"] >> 26) == 0x3c and
                    (r["a"] & 0xffff) == 0xe23 and
                    r["b"] == 0xfffffff007ac061c)
        fault = match(0x21e1, lambda r: r["el"] == 8 and
                      (r["a"] >> 26) != 0x3c)
        kind = (match(0x21e5, lambda r: r["a"] == 1 and
                      r["b"] == 0xfffffff007ac061c and r["c"] == 0x3c)
                if brk else
                match(0x21e5, lambda r: r["a"] == 2 and
                      r["c"] != 0x3c and
                      any(t["event"] == 0x21e1 and t["b"] == r["b"] and
                          t["a"] >> 26 == r["c"] for t in selected)))
        returned = match(0x20a3, lambda r: r["el"] == 8 and r["a"] == 8)
        restored = match(0x21e8, lambda r: r["el"] == 8 and
                         (r["a"], r["b"], r["c"]) == (1, 1, 1))
        done = all(match(e, lambda r: r["a"] == 0)
                   for e in (0x20ff, 0x19ff))
        valid = (ordered and copies and admission and vectors and armed and
                 preflight and el1 and mmu and translation and origin and
                 hvc and (brk or fault) and kind and returned and restored and
                 done and len(records) == len(selected) and
                 not (corrupt or noise or gaps or duplicates or regressions)
                 and not any(r["event"] == 0x19ee for r in selected))
        verdict = ("HIGH-VA C CHECKPOINT PASS" if brk else
                   "EARLY XNU EXCEPTION CAPTURED" if fault else "NOT VERIFIED")
        print("STAGE21-D " + (verdict if valid else
              f"NOT VERIFIED ordered={ordered} copies={copies} "
              f"vectors={vectors} trap={brk} fault={fault} "
              f"mmu={mmu} hvc={hvc} restored={restored} done={done}"))
        if not valid:
            raise SystemExit(1)
    if args.require_stage21c2:
        selected = [r for r in records if r["stage"] == 25]
        required = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                    0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                    0x1952, 0x21d6, 0x21d0, 0x20a0, 0x21da,
                    0x20a1, 0x21d1, 0x21d2, 0x21d3, 0x21d4,
                    0x21d8, 0x21d5, 0x20b3, 0x20a3, 0x21d7,
                    0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in required) and all(
            first[a] < first[b] for a, b in zip(required, required[1:]))
        def seen(event, predicate):
            return any(r["event"] == event and predicate(r) for r in selected)
        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        admission = (seen(0x1901, lambda r: r["a"] == 1) and
                     seen(0x1910, lambda r: r["a"] == 1) and
                     seen(0x1911, lambda r: r["a"] == 0))
        vector = seen(0x21d6, lambda r: r["el"] == 8 and
                      (r["a"], r["b"], r["c"]) ==
                      (0x81429000, 0xfffffff007429000, 0xd4000422))
        armed = seen(0x21d0, lambda r: r["el"] == 8 and
                     (r["a"], r["b"], r["c"]) ==
                     (0x8142d488, 0x8142f490, 0x14000719))
        preflight = seen(0x21da, lambda r: r["el"] == 8 and
                         r["a"] == 0x8142f490 and
                         r["b"] == 0xfffffff007429000)
        el1 = seen(0x20a1, lambda r: r["el"] == 4 and
                   (r["a"], r["b"], r["c"]) ==
                   (4, 0x8142d488, 0x824bc000))
        trapped = seen(0x21d1, lambda r: r["el"] == 8 and
                       (r["a"] >> 26) == 0x3c and
                       (r["a"] & 0xffff) == 0xe22 and
                       r["b"] == 0x8142f490)
        mmu = seen(0x21d2, lambda r: r["el"] == 8 and
                   (r["a"], r["b"], r["c"]) ==
                   (0xfffffff007429000, 0x22a5192519, 0x34d4593d))
        regs = seen(0x21d3, lambda r: r["el"] == 8 and
                    (r["a"], r["b"], r["c"]) ==
                    (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff))
        roots = seen(0x21d4, lambda r:
                     (r["a"], r["b"], r["c"]) ==
                     (0x8117c003, 0x8117d003, 0x81000601))
        blocks = seen(0x21d8, lambda r:
                      (r["a"], r["b"], r["c"]) ==
                      (0x81000601, 0x8117e003, 0x9dc00601))
        origin = seen(0x21d5, lambda r:
                      r["a"] == 0xfffffff007429004 and
                      (r["b"] & 0xf) == 5 and
                      (r["c"] & 0xf) == 4)
        hvc = seen(0x20b3, lambda r: r["el"] == 8 and
                   (r["a"] >> 26) == 0x16 and
                   (r["a"] & 0xffff) == 0x21 and
                   r["b"] == 0xfffffff007429004 and
                   (r["c"] & 0xf) == 5)
        returned = seen(0x20a3, lambda r: r["el"] == 8 and r["a"] == 8)
        restored = seen(0x21d7, lambda r:
                        (r["a"], r["b"], r["c"]) == (1, 1, 1))
        finished = all(seen(e, lambda r: r["a"] == 0)
                       for e in (0x20ff, 0x19ff))
        passed = (ordered and copies and admission and vector and armed and
                  preflight and el1 and trapped and mmu and regs and roots and
                  blocks and origin and hvc and returned and restored and
                  finished and len(selected) == len(records) and
                  not (corrupt or noise or gaps or duplicates or regressions) and
                  not any(r["event"] == 0x19ee for r in selected))
        print("STAGE21-C2 " +
              ("POST-MMU EL1t VIRTUAL-VECTOR GATE PASS" if passed else
               f"NOT VERIFIED ordered={ordered} copies={copies} "
               f"admission={admission} vector={vector} armed={armed} "
               f"preflight={preflight} trap={trapped} mmu={mmu} "
               f"regs={regs} tables={roots and blocks} HVC={origin and hvc} "
               f"return={returned} restored={restored} done={finished}"))
        if not passed:
            raise SystemExit(1)
    if args.require_stage21c1:
        selected = [r for r in records if r["stage"] == 24]
        required = (0x1900, 0x1901, 0x1910, 0x1912, 0x1911,
                    0x1930, 0x1940, 0x1941, 0x1950, 0x1951,
                    0x1952, 0x21c6, 0x21c0, 0x20a0, 0x21ca, 0x20a1,
                    0x21c1, 0x21c2, 0x21c3, 0x21c4, 0x21c7,
                    0x21c8, 0x21c9, 0x20b3, 0x20a3, 0x21c5,
                    0x20ff, 0x19ff)
        first = {}
        for index, rec in enumerate(selected):
            first.setdefault(rec["event"], index)
        ordered = all(e in first for e in required) and all(
            first[a] < first[b] for a, b in zip(required, required[1:]))
        def observed(event, predicate):
            return any(r["event"] == event and predicate(r) for r in selected)
        copies = ([r["a"] for r in selected if r["event"] == 0x1940]
                  == list(range(10)) and
                  [r["a"] for r in selected if r["event"] == 0x1941]
                  == list(range(10)))
        admission = (observed(0x1901, lambda r: r["a"] == 1) and
                     observed(0x1910, lambda r: r["a"] == 1) and
                     observed(0x1911, lambda r: r["a"] == 0))
        vector = observed(0x21c6, lambda r: r["el"] == 8 and
                          (r["a"], r["b"], r["c"]) ==
                          (0x8142e000, 0x8142e000, 0xd4000422))
        armed = observed(0x21c0, lambda r: r["el"] == 8 and
                         (r["a"], r["b"], r["c"]) ==
                         (0x8142d488, 0x8142f474, 0x14000719))
        preflight = observed(0x21ca, lambda r: r["el"] == 8 and
                            r["a"] == 0x8142f474 and
                            r["b"] == 0x8142e000)
        el1 = observed(0x20a1, lambda r: r["el"] == 4 and
                       (r["a"], r["b"], r["c"]) ==
                       (4, 0x8142d488, 0x824bc000))
        trapped = observed(0x21c1, lambda r: r["el"] == 8 and
                           (r["a"] >> 26) == 0x3c and
                           (r["a"] & 0xffff) == 0xe21 and
                           r["b"] == 0x8142f474)
        mmu = observed(0x21c2, lambda r: r["el"] == 8 and
                       (r["a"], r["b"], r["c"]) ==
                       (0x8142e000, 0x22a5192519, 0x30d00800))
        regs = observed(0x21c3, lambda r: r["el"] == 8 and
                        (r["a"], r["b"], r["c"]) ==
                        (0x8117a000, 0x8117b000, 0x0c08044f00bb44ff))
        roots = observed(0x21c4, lambda r:
                         (r["a"], r["b"], r["c"]) ==
                         (0x8117c003, 0x8117d003, 0x8117e003))
        blocks = observed(0x21c7, lambda r:
                          (r["a"], r["b"], r["c"]) ==
                          (0x81000601, 0x81000601, 0x6000005e600605))
        tail = observed(0x21c8, lambda r:
                        (r["a"], r["b"], r["c"]) ==
                        (0x6000005e800605, 0, 0x9dc00601))
        hvc_origin = observed(0x21c9, lambda r:
                              r["a"] == 0x8142e004 and
                              (r["b"] & 0xf) == 5 and
                              (r["c"] & 0xf) == 4)
        hvc = observed(0x20b3, lambda r: r["el"] == 8 and
                       (r["a"] >> 26) == 0x16 and
                       (r["a"] & 0xffff) == 0x21 and
                       (r["c"] & 0xf) == 5)
        returned = observed(0x20a3, lambda r: r["el"] == 8 and
                            r["a"] == 8)
        restored = observed(0x21c5, lambda r:
                            (r["a"], r["b"], r["c"]) == (1, 1, 1))
        finished = all(observed(e, lambda r: r["a"] == 0)
                       for e in (0x20ff, 0x19ff))
        passed = (ordered and copies and admission and vector and armed and
                  preflight and
                  el1 and trapped and mmu and regs and roots and blocks and
                  tail and hvc_origin and hvc and returned and restored and
                  finished and not (corrupt or noise or gaps or duplicates or
                                    regressions) and len(selected) == len(records)
                  and not any(r["event"] == 0x19ee for r in selected))
        print("STAGE21-C1 " +
              ("PRE-MMU EL1t PAGE-TABLE GATE PASS" if passed else
               f"NOT VERIFIED ordered={ordered} copies={copies} "
               f"admission={admission} vector={vector} armed={armed} preflight={preflight} "
               f"EL1={el1} trap={trapped} mmu={mmu} regs={regs} "
               f"roots={roots} blocks={blocks} tail={tail} "
               f"HVC={hvc and hvc_origin} return={returned} "
               f"restore={restored} finish={finished}"))
        if not passed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
