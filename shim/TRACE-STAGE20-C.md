# Stage 20-C — XNU early startup and framebuffer trace

**2026-10-22: hardware PASS and TWRP recovery confirmed.** Originally
tested offline, then flashed to the verified cactus phone using fastboot
(send/write/reboot each OKAY). COM25 capture:
`../usb-captures/stage-cactus-20261022-182508-915/session-001-182538-398.bin`.
All **44 frames** were CRC-valid, with zero gaps, and
`--require-stage20c` returned **EARLY XNU FRAMEBUFFER/VBAR GATE PASS**.
The genuine XNU EL1 breakpoint fired at `0x8142f144`
(`ESR=0xf2000c20`). Framebuffer first/last pixels were
`0xffffffff`, saved x20 matched `boot_args=0x824bc000`, HVC
returned to EL2, and instruction restoration and both completion
markers passed. ADB confirmed TWRP recovery. Physical screen
visibility was not independently observed; memory evidence proves
the writes occurred.

Stage20-C reuses the confirmed Stage20-B physical XNU segment loader,
EL2-to-EL1 transition, COM25 CDC logger and return to TWRP. It executes
the actual early XNU code through its diagnostic framebuffer drawing
loop, then stops immediately **before** XNU overwrites `VBAR_EL1`.

The image is independent from the proven Stage20-B stage-21 artifact.

## Entry and target

- Real entry PA: `0x8142d488`; its `b +0x1c64` branches to `0x8142f0ec`.
- Early code at `0x8142f0ec`: clear OS debug lock, mask interrupts,
  save boot_args into `x20`, draw the 64 × 32 white marker and synchronize.
- Breakpoint PA: `0x8142f144`, original `msr vbar_el1, x0`
  (instruction `0xd518c000`).
- Inserted temporary instruction: `brk #0xc20`
  (`0xd4218400`). After the exception the shim restores the
  original `msr vbar_el1, x0` and attempts the existing TWRP chainload.
- The original XNU vector table is not installed during this test.

To prevent testing a changed payload accidentally, the offline preflight
and on-device admission gate pin all **23 exact ARM64 instructions**
from `0x8142f0ec` through `0x8142f144`. Their 92-byte SHA256 is
`baa9674ab5b60ca787be5b7c7d8f8e54b552f0f85f4f09e50535c85395529ebc`.

## Framebuffer and visibility

The physical cactus framebuffer is `[0x5e605000,0x5fdc0000)`,
720×1440, 32bpp, row stride 2880 bytes. XNU's early loop writes
white (`0xffffffff`) to the bottom-left **64×32 pixels**, at
rows 1408–1439. The first pixel is `0x5e9e3000`; the last sampled
pixel is `0x5e9f8dbc`. The runtime gate also validates these addresses
and the boot_args framebuffer dimensions before execution.

**Will it appear on the phone screen?** Possibly as a small white block
at the bottom-left, if the bootloader left the display enabled and
the controller is scanning that framebuffer. On cactus the panel may
still be disabled after reboot, and the image deliberately does not
initialize the panel or backlight. A black screen alone does not
indicate failure. The logger instead verifies that the first and last
marker pixels contain `0xffffffff`, and reports the saved boot_args
pointer from XNU's `x20`.

## USB event checkpoints

The CLOG `stage` field is **22** and USB decode uses
`--require-stage20c`. Stage19 payload-copy frames are retained.

| Event | Expected evidence |
|---|---|
| `0x20c0` | Entry `0x8142d488`, armed breakpoint `0x8142f144`, initial branch `0x14000719` |
| `0x20a0` | EL2 before ERET |
| `0x20a1` | Shim EL1 checkpoint, CurrentEL = 4 |
| `0x20c1` | EL1 BRK EC=0x3c, ISS=0xc20, ELR=`0x8142f144` |
| `0x20c2` | XNU `x20=0x824bc000`, first/last white pixels both `0xffffffff` |
| `0x20b3` | HVC #0x21 caught at EL2 |
| `0x20a3` | Return to EL2 (CurrentEL=8) |
| `0x20c3` | Original VBAR opcode restored, EL1 trap and HVC verified: (1,1,1) |
| `0x20ff`, `0x19ff` | Zero-status completion before TWRP |

An earlier exception may still emit `0x20c1` with a different
ESR/ELR. The strict decoder will reject it. Memory writes into the
existing framebuffer can change visible pixels but not flash storage.
An unrecoverable exception may require the established manual fastboot
recovery procedure.

## Local artifact and checks

From WSL Ubuntu in `ios-cactus/shim`:

```bash
python3 test_stage20c_entry.py
python3 test_stage20c_usb.py
./build_trace_probe.sh 22
```

- Image: `out/cactus/cactus-trace-stage-22-boot.img`
- Size: **45,504,512 bytes**
- SHA256: **a78876e8f95089c3a13f2c08a3c29b1e7ea12ab49caaf03db5fb625d5a607dc8**
- Android boot v2 structural validation: passed.
- Payload mutation/early-code validation: passed.
- Synthetic USB capture and negative cases: passed.
- Existing Stage20-B USB decoder tests and ring tests: passed.
- AArch64 ELF disassembly verified the EL1 exception passes saved `x20`
  as the fourth argument before the HVC.

On a later authorized hardware test, start
`watch-cactus-usb.ps1 -Seconds 120` before flashing, then decode a
new COM25 session with:

```powershell
python 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\decode-cactus-usb.py' '<capture>.bin' --show 85 --require-stage20c
```

Confirm recovery is reachable afterward. Stage20-C only verifies
the early XNU instruction sequence through the VBAR boundary; it does
not establish MMU initialization or a booted XNU kernel.
