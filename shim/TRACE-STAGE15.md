# Stage 15: real cactus shim CDC preflight

Status 2026-10-22: **built and host-tested only**. The Stage 15 images
have not been flashed; Windows has not yet captured Stage 15 checkpoints.

## What changed

- `start.S`: install VBAR_EL2 at observed EL2 LK entry, VBAR_EL1 at
  EL1 entry, and VBAR_EL1 before the proven EL2-to-EL1 handoff.
- `shim.c`: initialize CDC before display diagnostics, record exception
  syndrome/PC/address/state from the **current** exception level, and
  drain a bounded amount of trace data.
- `fdt_ranges.[ch]`: bounded Benesse-style parser for live FDT memory,
  reserved-memory children and memreserve entries. Build a safe XNU
  RAM window plan by excluding the FDT, initrd payload and shim image.
- `loader.c`: verify the entire XNU-managed RAM window and reject
  overlapping segments or a kernel entry in non-file-backed bytes;
  emit DTB, payload, layout, segment, boot_args and XNU branch events.
- `stage15.[ch]`: reuse Stage 14's hardware-positive polled MUSB CDC
  implementation and 56-byte CRC-protected CLOG v2 frames (stage=15).
  Hold early records up to six seconds for USB configuration + DTR.

USB is serviced only by the shim when it calls the logger. The ring
lives in shim BSS and **is not reserved/passed to XNU yet**. The full
image can show shim-owned EL1 readiness immediately before branching to
XNU; Stage 16 still needs the **first XNU-owned instruction marker**.
The framebuffer may remain black even when the USB preflight succeeds.

## Build artifacts

Use the **preflight** image for the first device test:

`shim/out/cactus/shim-stage15-preflight-boot.img`

SHA256: `e4af2303aa57fd5143952db6188a4192fdf40125fc4433a6403b8b0507e70589`

It includes `out/cactus/payload.bin`, validates FDT and payload memory
layout and then stops without copying the XNU kernel or branching into
it. On validation failure, it emits `S15_ERROR (0x15e0)` with a reason
code and `S15_DIAG_STOP (0x15ff)` with reason=1.

`shim/out/cactus/shim-stage15-xnu-boot.img`

SHA256: `10c8e7ce46a5ec02895a821bc3b1e050693f44cff70d3c8dedec5bab468c7697`

This **instrumented full-XNU image is offline only**. Do not flash it
until the preflight passes on the live cactus FDT, because the original
stock cactus DTB reports **512 MiB** of RAM (one bank starting
`0x40000000`, size `0x20000000`) while the current XNU payload
requests `[0x70000000, 0xC0000000)`. LK may modify the FDT at boot;
the static DTB alone cannot settle the live RAM layout.

Both images were locally built using WSL Ubuntu's AArch64 compiler,
packed with cactus Android boot **v2**, and passed `check_image.py`.

## Hardware test procedure (when fastboot is confirmed)

Start the Windows capture before reboot:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
Start-Process powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass',
  '-File', ('"' + "$root\ios-cactus\shim\capture-cactus-trace.ps1" + '"'),
  '-Seconds','45','-Output',('"' + "$root\stage15-preflight-raw.bin" + '"')) `
  -RedirectStandardOutput "$root\stage15-preflight-usb.log" `
  -RedirectStandardError "$root\stage15-preflight-usb.err" -WindowStyle Hidden
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "$root\ios-cactus\shim\out\cactus\shim-stage15-preflight-boot.img"
fastboot -s 3184a8a17d2b reboot
```

Decode with:

```powershell
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\stage15-preflight-raw.bin" --show 100
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\stage15-preflight-raw.bin" --require-stage15
```

Expected ordered checkpoints include `0x1500` entry, `0x1501`
configured USB, `0x1502` live FDT, `0x1503` payload, `0x1504`
approved memory plan, and `0x15ff` diagnostic stop with a zero reason.
`--require-stage15` deliberately fails if any of these are missing.
If `0x15e0` appears, its first argument is the preflight error code:
`1` invalid FDT, `2` RAM overlap/reservation, `3` segment overlap.
Use the existing recovery path if the diagnostic does not reboot.

## Local verification

```bash
python3 shim/test_stage15_fdt.py
python3 shim/test_cactus_trace_ring.py
python3 shim/test_cactus_usb_decode.py
cd shim
BOARD=cactus STAGE15=1 STAGE15_DIAG=1 TAG=stage15-preflight ./build.sh out/cactus/payload.bin
BOARD=cactus STAGE15=1 STAGE15_DIAG=0 TAG=stage15-xnu ./build.sh out/cactus/payload.bin
```

Host tests do not establish live FDT contents, CDC enumeration from the
real shim, automatic TWRP return, or XNU execution. Those remain
hardware acceptance gates.
