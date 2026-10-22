# Stage 15 hybrid: isolate the working entry wrapper

**2026-10-22: HARDWARE USB POSITIVE — completion marker still missing.**

The Redmi 6A accepted the image and rebooted (both fastboot operations
OKAY). Windows opened **COM25** and captured **2,072 bytes**, containing
37 CRC-valid `stage=15` frames with no gaps or duplicates: entry
`0x1580`, USB-ready `0x1581`, and 35 changing heartbeats `0x1582`.
The entry reported EL2=0x8; the session was `0xca15e001`.
The raw capture is `../stage15-hybrid-raw.bin` (SHA256
`d697f9f9b212e1f1001897546af0e37ee8c81ec6de8b75e4627e7df39fa078a8`).
The stream ended without event `0x158a`, so the full timed probe is
**not yet verified**. No XNU handoff or TWRP return was confirmed.
This is the first successful USB transmission by the Stage15 C logger
inside the working Stage14 recovery wrapper.

The Stage 14 recovery-wrapped diagnostic successfully sent 99 binary
CLOG records over normal USB. All Stage 15 actual-shim variants,
including the one containing *only* early USB without a payload, failed
to enumerate CDC. These tests never ran XNU, so blaming XNU for the
black screen is premature. Fastboot itself can be usable with the
display off.

## New controlled experiment

The two decompressed Linux Images differ fundamentally:

- Proven Stage 14 wrapper: **31,728,535 bytes**, Linux header
  `image_size=31,728,535`. It embeds TWRP's recovery Image, jumps
  to a probe near the end of the Image, and later copies the recovery
  kernel back over the original Image entry.
- Failed actual Stage 15 shim: **172,032 bytes**, Linux header
  `image_size=172,032`, branches to `_start` at offset `0x800`.
- Both use ARM64 Image magic `ARMd`, flags `0xa` and cactus
  Android boot header v2. Header compliance did not prove execution.

New `STAGE=15` in `linux_stage_probe.S` deliberately keeps the
Stage 14 entry, recovery-Image padding, DTB, ramdisk sentinel,
USB PHY context and TWRP chainloader. Instead of
`stage14_usb_probe`, it calls the previously built
`stage15_early_main(dtb)`, which uses the same
`cactus_usb.[ch]` and `cactus_trace.[ch]` driver, encodes Stage 15
binary records, and runs only a bounded early USB test.

Built image:
`shim/out/cactus/cactus-trace-stage-15-boot.img`

- Size: **28,956,672 bytes** (same as Stage 14)
- SHA256: `07bde34f600187c9208f9c83b601af835217576838cfd9504b543423bd322334`
- AArch64 link: `_start=0`, `probe=0x1e37048`,
  `stage15_early_main=0x1e41fe8`, all within validated Image size.
- Android boot v2 layout validator PASS.
- Host CLOG decoder tests PASS.

**Interpretation:** If this hybrid enumerates CDC and transmits
Stage 15 records, then the real shim's separate entry/layout is the
leading explanation for its silence. Next transplant the real shim's
startup and loader into the proven recovery wrapper one checkpoint at
a time, preserving the same entry until we identify the issue. If the
hybrid also fails, compare the already successful Stage 14 image and
Stage 15 hybrid for differences in C startup, BSS, USB timing and
reset behavior. Neither result alone proves XNU itself works.

## When the phone is returned to fastboot

Start capture **before** reboot, in one PowerShell terminal:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
powershell -NoProfile -ExecutionPolicy Bypass -File "$root\ios-cactus\shim\capture-cactus-trace.ps1" -Seconds 50 -Output "$root\stage15-hybrid-raw.bin"
```

In another terminal:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "$root\ios-cactus\shim\out\cactus\cactus-trace-stage-15-boot.img"
fastboot -s 3184a8a17d2b reboot
```

Decode after capture:

```powershell
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\stage15-hybrid-raw.bin" --require-stage15-early --early-level 1
```

Pass requires valid Stage 15 entry `0x1580`, USB `0x1581`,
varying heartbeat `0x1582` and final event `0x158a`.
The recovery handoff is intended but must be observed separately.
**No LK flash or XNU kernel copy is part of this test.**
