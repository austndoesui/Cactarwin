# Stage 16: real shim FDT and debug inside the proven USB wrapper

**2026-10-22: BUILT OFFLINE. Hardware testing is still pending.**

## Purpose

Stage 15 Hybrid transmitted **37 CRC-valid** Stage 15 records through
the TWRP/recovery-wrapped entry; the user's manual reboot may explain
its missing DONE record. Standalone shim Linux Images did not enumerate
USB, so Stage 16 deliberately keeps the known working boot packaging.

Stage 16 links and **executes the genuine cactus shim components**
`fdt.c:fdt_check`, `fdt_ranges.c:fdt_inspect` /
`fdt_region_free`, and `debug.c:dbg_init` while the same polled MUSB
CDC USB logger streams CLOG v2 records. It reads LK's **live** device
tree at the entry x0 pointer (Stage 15 observed `0x47880000`),
enumerates RAM and reserved-memory ranges, and checks whether the
existing XNU RAM window `[0x70000000,0xc0000000)` is wholly free.

All memory ranges are inspected, **never written or allocated**.
The only intentional non-USB write outside probe memory is the
existing `dbg_init` reserved-RAM trace setup at `0x47c90000`;
this operation is bracketed by before/after USB checkpoints.
The assembly entry now selects VBAR_EL2 at LK's observed EL2, with
an exception logger that records ESR/ELR/FAR if a trap occurs after
USB is working. The recovery chainloader remains in place.

There is **no XNU payload copy, no XNU execution, no MMU enable,
no EL2→EL1 transition and no LK modification** in this image.
These steps must be added only after we know the live RAM layout.

## Build and image

From WSL Ubuntu:

```bash
cd '/mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim'
./build_trace_probe.sh 16
```

Flash candidate: `shim/out/cactus/cactus-trace-stage-16-boot.img`

- Size **28,958,720 bytes**.
- SHA256 `315120457a337e322f23b0a24bdf38e9f7b2b1a94d560bf23808133385b63668`.
- Android boot **v2** structural validation passed.
- Verified link symbols for `stage16_probe`, `fdt_check`,
  `fdt_inspect`, `dbg_init` and `usb_init`.
- Python CLOG decoder regression tests passed.

## CLOG event IDs (all with stage byte 16)

| Event | Meaning and three 64-bit arguments |
| --- | --- |
| `0x1600` | Entry: live DTB address, CurrentEL, CNTFRQ |
| `0x1601` | USB initialized: success flag, MUSB HWVERS, POWER |
| `0x1602` | Heartbeat: sequence, host DTR, dropped/pending |
| `0x1610` | Before FDT header read: DTB pointer |
| `0x1611` | Real shim FDT header result: valid flag, total bytes, DTB |
| `0x1612` | Benesse-derived FDT parse: valid flag, RAM count, reserved count |
| `0x1613` | RAM bank: index, base, size |
| `0x1614` | Reserved memory: index, base, size (up to first 8) |
| `0x1615` | LK initrd: start, end |
| `0x1616` | XNU memory window: free flag, base `0x70000000`, size `0x50000000` |
| `0x1620` | Before real `dbg_init`, DBG_BASE |
| `0x1621` | After real `dbg_init`, DBG_BASE and DBG_MAGIC |
| `0x16ee` | CPU exception: ESR, ELR, FAR; final record if servicing survives |
| `0x16ff` | Diagnostic DONE: FDT parse result, heartbeat count, dropped/pending |

The timed sequence reaches the FDT at approximately seven seconds
after USB initialization, then logs the real shim operations and
should complete in roughly 14–18 seconds before attempting recovery.
**Allow at least 30 seconds** before a manual fastboot reboot.
If the host doesn't catch every milestone, inspect the last valid
record and compare it with the next operation to narrow the failure.
Absent DONE alone doesn't prove a shim crash if you rebooted early.

## First hardware capture after restoring fastboot

Start capture in Windows PowerShell terminal A **before reboot**:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
powershell -NoProfile -ExecutionPolicy Bypass -File "$root\ios-cactus\shim\capture-cactus-trace.ps1" -Seconds 55 -Output "$root\stage16-raw.bin"
```

In terminal B, verify `product: cactus`, flash and reboot:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "$root\ios-cactus\shim\out\cactus\cactus-trace-stage-16-boot.img"
fastboot -s 3184a8a17d2b reboot
```

After the capture:

```powershell
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\stage16-raw.bin" --show 80 --require-stage16
```

The strict Stage 16 gate requires valid USB records, ordered FDT and
debug checkpoints, successful FDT parsing and DONE without CRC or
sequence errors. If it fails, the decoded checkpoints still provide
the usable evidence. TWRP return and XNU start require separate proof.

## Decision after the device test

- If live FDT parsing succeeds, inspect the RAM banks and
  `0x1616` verdict. Adjust the XNU payload's physical range and
  boot_args to match **verified** memory before the loader copies
  anything.
- If `0x1620` occurs without `0x1621`, inspect reserved debug RAM
  initialization and the exception record.
- If USB never opens, regress to the hardware-positive Stage 15 Hybrid
  entry and compare the Stage 16 vector/linked-code changes.
- Once the live preflight is hardware-positive, add a bounded EL2→EL1
  checkpoint in the wrapper and then an **XNU-owned first instruction**
  marker. USB logging after the XNU MMU change remains separate work.
