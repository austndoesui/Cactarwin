# Stage 15 Early USB: isolate actual shim entry from early MTK MMIO faults

Status 2026-10-22: **built and validated offline; no Early USB image
has been flashed or demonstrated working on the device yet**.

## Reason for this split

- Stage 14's separate Linux/TWRP probe produced 99 valid CLOG v2
  events over Windows COM25. The Stage 15 preflight built from the
  actual XNU shim reached no COM port and delivered 0 bytes.
- Stage 15 previously ran `dbg_init`, `dbg_mark`, `wdt_init` and
  `mark` **before** starting USB. Its zero-byte result did not prove
  whether the real shim reached `stage15_begin`.
- `STAGE15_EARLY` reroutes the **actual** `start.S` Linux-image entry:
  set stack and EL2/EL1 vectors, clear BSS, call
  `stage15_early_main(dtb)`, then stop. The disassembly was inspected:
  it branches directly to the probe and past `shim_main`.
- Stage 15 Early uses the exact Stage 14-verified
  `cactus_usb.[ch]` PHY/EP0 and `cactus_trace.[ch]` CRC binary
  framing. Its stage byte is 15 and session ID differs by level.

## A/B probes (one operation added at each level)

| Mode | Added behavior after eight seconds of USB activity |
| --- | --- |
| 1 | USB only. No debug RAM, watchdog, framebuffer, FDT or XNU. |
| 2 | Mode 1 + `dbg_init()` with BEFORE/AFTER USB records. |
| 3 | Mode 2 + `wdt_init(20)`, periodic ping, bounded drain and disable. |
| 4 | Mode 3 + live `fdt_check`/`fdt_inspect`; records RAM banks. |

Modes 2–4 also have **payload-size-matched** builds to keep the same
16,540,608-byte SWEETXNU ramdisk as failed Stage 15 preflight. None
read, relocate, copy, execute or branch into XNU. The payload is
present only to control for boot-image size and LK loading behavior.
No part of this series patches or executes code in `lk.img`.

First test **level 1 with matched payload**, then level 1 without
payload if USB is absent. This distinguishes a large boot-image/layout
effect from real shim entry failure. Once level 1 passes, move to
level 2, then 3, then 4, stopping at the first missing AFTER record.

## Built images (cactus Android boot header v2 check passed)

| Mode | Image under `shim/out/cactus/` | SHA256 |
| --- | --- | --- |
| 1 small | `shim-stage15-early-usb-boot.img` | `f3bf69ff42e5e5437a9e858f9cc83b785ebfea906e079226a51d8c5b7784ce9c` |
| 1 matched | **`shim-stage15-early-payload-boot.img`** | `613fffd85f6082b35acf3398eafcdc721858135b9c24ad2700f373a4264a9d0c` |
| 2 matched | `shim-stage15-early-debug-payload-boot.img` | `434f0f30dfd29452798bcb28d048b71444ff3597f87855c280ac8aa067a5dff0` |
| 3 matched | `shim-stage15-early-watchdog-payload-boot.img` | `dd61e8a64b8dfc541d336c70619f5e83c2fd515a911cb39a7c20ef929f3dbfb2` |
| 4 matched | `shim-stage15-early-fdt-payload-boot.img` | `6cd340ff1e7be879beec292e1c16f2046005ed521c611e0499dc6b5780a7f3fb` |

Small variants also exist for modes 2–4, but the matched versions
above are the useful controlled comparison to the failed preflight.

## What Windows should receive

- `0x1580`: boot entry (DTB pointer, CurrentEL, MPIDR).
- `0x1581`: USB init status, controller HWVERS/POWER.
- `0x1582`: changing heartbeat, CDC DTR state, dropped and queued.
- `0x1583/84`: before/after debug initialization (mode >= 2).
- `0x1585/86`: before/after watchdog setup (mode >= 3).
- `0x1587/88`: before/after live FDT parsing (mode >= 4).
- `0x1589`: FDT RAM bank base and length (mode 4, if accepted).
- `0x15ef/ee`: exception vector, ESR, ELR, FAR, SPSR when fault
  handler is able to service the USB controller.
- `0x158a`: normal diagnostic end and level.

All frames are 56-byte CRC-verified binary `CLOG` events. Earlier
already-sent bytes remain in the host capture if a later stage hangs.
The probe retains its first records until the host opens CDC with
DTR; heartbeat continues throughout the bounded test. It cannot
capture a fault that prevents stack/BSS setup or USB enumeration.
The phone is expected to require manual recovery/fastboot afterward.

## First hardware test when fastboot is connected

In PowerShell terminal **A**, start recording before device reboot:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
powershell -NoProfile -ExecutionPolicy Bypass -File "$root\ios-cactus\shim\capture-cactus-trace.ps1" -Seconds 45 -Output "$root\stage15-early-payload-raw.bin"
```

In another PowerShell terminal **B** after verifying `product: cactus`:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "$root\ios-cactus\shim\out\cactus\shim-stage15-early-payload-boot.img"
fastboot -s 3184a8a17d2b reboot
```

After capture finishes, decode:

```powershell
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\stage15-early-payload-raw.bin" --show 70 --require-stage15-early --early-level 1
```

**Pass:** Stage15 `0x1580`, `0x1581`, a changing heartbeat stream and
`0x158a`, with valid CRC and no out-of-order/duplicate sequences.
If enumeration never starts, try the *small* level-1 image when
fastboot is restored. If it also fails, investigate the real shim
header/startup/vector/stack path (it differs from Stage 14's recovery
chainload probe) before touching the LK partition.

## Offline reconstruction

```bash
cd '/mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim'
BOARD=cactus STAGE15=1 STAGE15_EARLY=1 TAG=stage15-early-payload ./build.sh out/cactus/payload.bin
BOARD=cactus STAGE15=1 STAGE15_EARLY=2 TAG=stage15-early-debug-payload ./build.sh out/cactus/payload.bin
BOARD=cactus STAGE15=1 STAGE15_EARLY=3 TAG=stage15-early-watchdog-payload ./build.sh out/cactus/payload.bin
BOARD=cactus STAGE15=1 STAGE15_EARLY=4 TAG=stage15-early-fdt-payload ./build.sh out/cactus/payload.bin
```

The static cactus DTB test still only exposed
`0x40000000–0x60000000` as DRAM, while the XNU payload requests
`0x70000000–0xC0000000`. The level-4 live FDT record will establish
what LK actually passes; no XNU memory copies should occur until
that conflict is understood.
