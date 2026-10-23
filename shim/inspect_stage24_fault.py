#!/usr/bin/env python3
"""Inspect the actual stage18 XNU Mach-O metadata and first undefined PC."""
from pathlib import Path
import argparse
import struct
import zlib

from check_stage21b_mmu import Payload

FAULT_VA = 0xFFFFFFF0072A9E64
UNDEFINED = 0xE7FFDEFF


def inspect(data: bytes):
    p = Payload(data)
    off = p.segs[0][2]
    hdr = struct.unpack_from('<8I', data, off)
    if hdr[0] != 0xFEEDFACF:
        raise ValueError('payload first segment is not Mach-O')
    cmdptr = off + 32
    named = []
    symtab = None
    for _ in range(hdr[4]):
        cmd, size = struct.unpack_from('<II', data, cmdptr)
        if cmd == 0x19:
            name = data[cmdptr+8:cmdptr+24].split(b'\x00')[0].decode()
            addr, vmsize, fileoff, filesize = struct.unpack_from('<4Q', data, cmdptr+24)
            named.append((name, addr, vmsize, fileoff, filesize))
        elif cmd == 2:
            symtab = struct.unpack_from('<4I', data, cmdptr+8)
        cmdptr += size
    pa = p.phys + FAULT_VA - p.virt
    if p.word(pa) != UNDEFINED:
        raise ValueError('faulting opcode changed')
    return p, named, symtab


def symbols(p, named, symtab):
    if not symtab:
        raise ValueError('this XNU payload has no symbol table')
    linkedit = next((s for s in named if s[0] == '__LINKEDIT'), None)
    if not linkedit:
        raise ValueError('this XNU payload has no LINKEDIT segment')
    packed = next((s for s in p.segs if s[0] == linkedit[1]), None)
    if packed is None:
        raise ValueError('LINKEDIT missing from packed payload')
    symoff, nsyms, stroff, strlen = symtab
    def packed_offset(file_off, length):
        if file_off < linkedit[3] or file_off + length > linkedit[3] + linkedit[4]:
            raise ValueError('symbol/string data outside LINKEDIT')
        return packed[2] + file_off - linkedit[3]
    so = packed_offset(symoff, nsyms*16)
    st = packed_offset(stroff, strlen)
    names = p.data[st:st+strlen]
    syms = []
    for i in range(nsyms):
        idx, kind, section, flags, value = struct.unpack_from('<IBBHQ', p.data, so+i*16)
        if not (kind & 0xe) or idx >= len(names) or not value:
            continue
        end = names.find(b'\0', idx)
        if end < 0:
            continue
        sym = names[idx:end].decode('utf-8', errors='replace')
        syms.append((value, sym))
    syms.sort()
    return syms


def symbol_context(p, named, symtab, address=FAULT_VA):
    syms = symbols(p, named, symtab)
    before = [s for s in syms if s[0] <= address]
    after = [s for s in syms if s[0] > address]
    if not before:
        raise ValueError('no symbol preceding fault')
    return before[-6:], after[:6]


def decode_capture(data, syms):
    if len(data) % 56:
        raise ValueError('CLOG file has an incomplete frame')
    by_event = {}
    for start in range(0, len(data), 56):
        record = data[start:start+56]
        if record[6] != 29 or zlib.crc32(record[:52]) != struct.unpack_from('<I',record,52)[0]:
            raise ValueError(f'invalid Stage29 CLOG frame at offset {start:#x}')
        event, = struct.unpack_from('<I', record, 16)
        by_event.setdefault(event, []).append(struct.unpack_from('<QQQ', record,28))
    print('Stage24 XNU register context:')
    for event in range(0x2400,0x2408):
        if len(by_event.get(event,[])) != 1:
            raise ValueError(f'missing/duplicate Stage24 register event {event:#x}')
        vals = by_event[event][0]
        if event == 0x2400:
            labels = ('x29/frame pointer','x30/LR','SP_EL1')
        elif event == 0x2407:
            labels = ('x28','SP_EL0','stage')
        else:
            start = (event-0x2401)*3
            labels = tuple(f'x{i}' for i in (range(start,start+3)
                      if event < 0x2404 else range(start+10,start+13)))
        for name, value in zip(labels, vals):
            preceding = [s for s in syms if s[0] <= value]
            symbol = ''
            if preceding and 0xFFFFFFF000000000 <= value <= 0xFFFFFFFFFFFFFFFF:
                va, nm = preceding[-1]
                if value-va < 0x10000:
                    symbol = f' {nm}+{value-va:#x}'
            print(f'  {name:>18}={value:#018x}{symbol}')
    print('Validated XNU frame-pointer chain (if available):')
    for event in range(0x2410,0x2414):
        for fp, prev, lr in by_event.get(event, []):
            candidate = [s for s in syms if s[0] <= lr]
            addr_label = ''
            if candidate and lr - candidate[-1][0] < 0x10000:
                base, name = candidate[-1]
                addr_label = f' {name}+{lr-base:#x}'
            print(f'  frame={fp:#018x} prev={prev:#018x} '
                  f'caller={lr:#018x}{addr_label}')
    for fp, root, count in by_event.get(0x241f, []):
        print(f'  page-walk TTBR1={root:#018x}, frames={count}, last_fp={fp:#018x}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('payload', type=Path)
    ap.add_argument('--capture', type=Path,
                    help='symbolicate the first Stage29 COM25 register capture')
    args = ap.parse_args()
    p, named, symtab = inspect(args.payload.read_bytes())
    print('VA', hex(FAULT_VA), 'PA', hex(p.phys + FAULT_VA - p.virt))
    print('Mach-O segments:')
    for name, addr, size, fo, fs in named:
        print(f'  {name:<16} va={addr:#x} size={size:#x} off={fo:#x} bytes={fs:#x}')
    print('LC_SYMTAB', symtab)
    before, after = symbol_context(p, named, symtab)
    print('Symbols immediately before fault:')
    for a, name in before:
        print(f'  {a:#x} {name} +{FAULT_VA-a:#x}')
    print('Symbols immediately after fault:')
    for a, name in after:
        print(f'  {a:#x} {name}')
    pc = p.phys + FAULT_VA - p.virt
    for addr in range(pc - 0x34, pc + 0x40, 4):
        print(f'{addr:#x}: {p.word(addr):#010x}' + (' <-- FAULT' if addr == pc else ''))
    if args.capture:
        decode_capture(args.capture.read_bytes(), symbols(p,named,symtab))


if __name__ == '__main__':
    main()
