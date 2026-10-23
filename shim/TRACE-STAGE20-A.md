# Stage 20-A — EL2 → EL1 → EL2 proof, with TWRP recovery

**Status (2026-10-22): hardware PASS; successful TWRP return.**
Stage19-A had already passed physical segment copy / sampled data
checks. Stage20-A now proves execution of a shim-owned EL1 trampoline
and successful return to EL2 via HVC.

## Verified hardware result

- Device: Redmi 6A `cactus`, fastboot serial `<cactus-fastboot-serial>`.
- Fastboot flash to `boot` and reboot both returned `OKAY`.
- Capture: `../usb-captures/stage-cactus-20261022-170729-186/session-001-170820-150.bin`
  (40 CRC-valid frames / 2,240 bytes, no errors, gaps or duplicates).
- `--require-stage20` result: **STAGE20 EL1 ERET/HVC RETURN PASS**.
- `0x20a1`: `EL=4, a=4, b=0x8142d488, c=0x824bc000`.
- `0x20a2`: `EL=8, a=0x5a000020, c=0x600003c5`
  (ESR class 0x16 / HVC #0x20).
- `0x20a3`: `EL=8, a=8`. `0x20ff` and `0x19ff`: `a=0`.
- Windows ADB confirmed successful TWRP recovery chainload:
  `<cactus-fastboot-serial> recovery product:omni_cactus model:Redmi_6A`.
- **The first XNU instruction has still not run.** That is Stage20-B.

## What the Stage20-A image actually does

The tested Stage19 loader and its USB CDC logging are retained.
After verifying the live FDT memory map and copying the actual XNU
segments / Apple ADT / boot_args into the admitted 480 MiB DRAM window,
the Stage20 extension exercises only a **shim-owned trampoline**:

1. At EL2, write event `0x20a0` (XNU entry PA, boot_args PA, EL2).
2. Create an independent 8 KiB EL1 stack and install shim-owned EL1
   exception vectors. Set `HCR_EL2.RW`, `SCTLR_EL1`,
   `CNTHCTL_EL2`, `SPSR_EL2=0x3c5` (EL1h).
3. `ERET` into the **shim's own EL1 code**, which emits `0x20a1`.
   Its CLOG frame must have `EL=0x4` and value `a=4`.
4. Execute `HVC #0x20`; the EL2 vector verifies the trap's ESR
   exception class `0x16` and immediate `0x20`. It emits `0x20a2`
   with the ESR, ELR and original `SPSR_EL2`.
5. Set `SPSR_EL2=0x3c9` (EL2h) and `ERET` back to the shim's EL2
   continuation. Emit `0x20a3` with `a=8`; then emit `0x20ff a=0`
   and `0x19ff a=0`.
6. Continue the existing bounded USB polling and Linux/TWRP chainload.

**XNU instructions are not executed in Stage20-A.** An unexpected
exception records `0x19ee` with ESR/ELR/FAR before halting. The
physical XNU entry at `0x8142d488` is emitted as *data*, not branched
to. This separation makes the future Stage20-B fault attribution
possible.

The same recovery boot header v2 and USB serial hardware transport
from the Stage18/19 successes are reused. The CLOG ring is enlarged to
64 slots for Stage20 to retain all 10 paired segment copy markers plus
the EL1/HVC signals if the host enumerates late.

## Built image, offline checks

- Builder: `cd /mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim && ./build_trace_probe.sh 20`
- Image: `out/cactus/cactus-trace-stage-20-boot.img`
- Size: **45,502,464 bytes**
- SHA256: `b078871f8e8fd80e5772744a24835ab634565e3746614959094c6838a1bab37c`
- Android boot image v2: validated by `check_image.py`.
- AArch64 ELF disassembly verified `ERET` into `stage20_at_el1`,
  `HVC #0x20`, the HVC class/ISS checks, return `ERET` into
  `stage20_resume_el2`, and normal function return.
- USB decoder: `decode-cactus-usb.py SESSION.bin --require-stage20 --show 80`;
  it requires all ten segment start/done pairs, admission success,
  the EL=4 event, valid HVC ESR, EL2 recovery, two zero-status DONE
  checkpoints and no loss/corruption.

The hardware trace now independently establishes EL1 execution for
the shim trampoline. It does not establish XNU execution. Do not
interpret the calculated XNU entry marker as an XNU instruction.

## Hardware procedure (when in verified cactus fastboot)

Start the reconnecting PowerShell capture before flashing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\watch-cactus-usb.ps1' -Seconds 120
```

In a second Windows terminal after verifying fastboot product:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
fastboot -s <cactus-fastboot-serial> getvar product
fastboot -s <cactus-fastboot-serial> flash boot "$root\ios-cactus\shim\out\cactus\cactus-trace-stage-20-boot.img"
fastboot -s <cactus-fastboot-serial> reboot
```

Decode a session saved by the watcher:

```powershell
python 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\decode-cactus-usb.py' '<capture>.bin' --show 80 --require-stage20
```

Verify that TWRP recovery is reachable afterward. If the phone hangs
or resets, use the previously proven manual fastboot recovery path.
The next, separately testable Stage20-B should branch into XNU only
after `0x20a1`, `0x20a2`, `0x20a3` and recovery are confirmed
on hardware.
