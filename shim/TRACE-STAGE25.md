# Stage25 / Stage30 — exact missing kernel PTE request

**2026-10-23 hardware Stage25 PASS:** authorized `fastboot flash boot`
and reboot both returned OKAY on `product: cactus` (serial
`<cactus-fastboot-serial>`). COM25 captured the exact missing framebuffer mapping.

Strict-valid capture:
`F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-212828-797\session-001-212942-364.bin`.
**77 valid CRC frames, 0 bad frames, gaps, duplicates or regressions**;
`--require-stage25` => `STAGE25 NULL-PTE REQUEST CAPTURED; COM25 HELD`.
Second session in this capture folder ended at preflight, with only
43 valid frames; it is **not** a second Stage25 fault confirmation.

**PROVEN MISSING MAPPING:**

- `x20` initial virtual address: `0xfffffff049dbb000`.
- `x19` first failing virtual address: **`0xfffffff04a000000`**.
- `x22` requested physical beginning: **`0x5e605000`** (cactus LK
  framebuffer start).
- `x21` requested physical end: `0x5e9fa000` (exclusive).
- Current TTBR1: `0x824c1000`; page-walk checked 1 descriptor.
- L1 descriptor slot: **`0x824c1e08`**, value **zero**, an invalid
  descriptor. The requested VA crosses a 1 GiB L1 boundary at
  `0xfffffff04a000000`, where XNU has not built a page-table subtree.

In `/home/lapdq/xnu-cactus/osfmk/arm64/arm_vm_init.c` around 2334-2350,
`va_l1_end` includes only a calculated base reserve plus
`round_page(args->Video.v_height * args->Video.v_rowBytes)` and rounds
up to 8 MiB before allocating tables. The live VA proves the window
does not cover the whole actual early `io_map()` allocation path for
the framebuffer. Next fix: calculate the required end VA from the
real early I/O mappings and ensure the kernel pmap bootstrap allocates
valid L1/L2/L3 tables across the boundary. Avoid arbitrarily writing
the missing descriptor: page tables must use a correctly allocated
L2 table and attributes, and the actual VM reservation must agree.

The phone was still absent from host fastboot when checked after this
COM25 run; Stage30 holds after the exception. Manual entry to fastboot
followed by `fastboot oem reboot-recovery` can restore TWRP.

Image `shim/out/cactus/cactus-trace-stage-30-boot.img`, 45,506,560 bytes,
SHA256 `65efa3fdbc9b5e28ca7e3298f9a5ca794939852ebf57a93001f2287d339b155b`.

Stage24/build29 proved that native XNU enters `_pmap_map_bd.cold.1` and
then panics due to a null page-table entry. The exact loaded Mach-O
has `_pmap_map_bd` at physical `0x81404f90` (VA
`0xfffffff007404f90`). Its failure branch at **PA `0x81405218`**, **VA
`0xfffffff007405218`**, has opcode `0x941ae25e` (BL to panic routine at
PA `0x81abdb90`). The stage30 shim verifies that opcode and the register
assignments, and substitutes BRK `#0xe25` (`0xd421c4a0`) at the BL
instruction *only for the live diagnostic*. After the EL2 capture it
restores the original BL and the virtual exception vectors. The real
startup code, symbol table and XNU payload remain otherwise unchanged.

The BRK lands before `panic()` destroys the useful call-site registers.
At the call, callee-saved XNU registers have the following meanings:

| Register | Meaning | COM25 event |
| --- | --- | --- |
| x19 | VA whose `pmap_pte(kernel_pmap, vaddr)` was null | `0x2502.a` |
| x20 | Initial requested VA | `0x2503.a` |
| x21 | End physical address (exclusive) | `0x2502.c` |
| x22 | Beginning physical address | `0x2502.b` |

`0x2500` proves that the original opcode was checked and BRK armed.
`0x2501` records ESR/ELR and whether the planned BRK fired.
`0x2504`..`0x2506` read the actual L1/L2/L3 descriptors for the failing
VA using the bounded physical TTBR1 walker: entries outside verified
DRAM are **never dereferenced**. `0x2507` records TTBR1 and the number
of inspected levels. `0x2508` proves the BL was restored. Stage24 CPU,
stack, exception, MMU, vector restoration and USB hold events remain.

The strict decoder prints the exact VA, physical range, and last
resolved PTE descriptor:

```powershell
python shim/decode-cactus-usb.py 'CAPTURE.bin' --require-stage25 --show 95
```

Expected verdict: `STAGE25 NULL-PTE REQUEST CAPTURED; COM25 HELD`.
All six targeted tests passed, including pmap opcode mutation guards,
capture omissions, address inconsistency and missing restoration.
The final WSL build passed `check_stage25_pmap.py`, Stage22 entry guards,
Android boot-image v2 structural verification and SHA256 hash output.

When the device is **actually detected in fastboot** (serial
`<cactus-fastboot-serial>`), start `watch-cactus-usb.ps1` before flashing, then
`fastboot -s <cactus-fastboot-serial> flash boot 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\out\cactus\cactus-trace-stage-30-boot.img'`
and `fastboot -s <cactus-fastboot-serial> reboot` while the watcher is running.
The diagnostic intentionally holds COM25 after the first failure; enter
fastboot manually and use `fastboot oem reboot-recovery` as needed.

The underlying root cause has not yet been repaired. The capture is
needed to decide whether `arm_vm_init.c` has created a too-small early
I/O L3 table window, whether `io_map` requests an unexpected range,
or whether a pmap root/descriptor is invalid. No broad speculative
page-table writes or debugger-opcode bypass are appropriate yet.
