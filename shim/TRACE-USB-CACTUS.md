# Redmi 6A USB 2.0 shim diagnostic (MediaTek MUSB)

**2026-10-22 HARDWARE PASS: USB raw binary transport proven.**
The MT6765 MUSB EP0 implementation lost the next 8-byte SETUP after
control-IN completion because the `STATUS_OUT` branch discarded every
RXPKTRDY. It now treats COUNT0==8 as a fresh SETUP and bounds the
RXPKTRDY acknowledgement delay to 50 ms, following the vendor gadget
driver's behavior. Flash of Stage 12 SHA256
`fadf08ae5cdf90b789ebd91eaa7648266c87356ba081eca4f31a3071ce97ea36`
succeeded. Windows enumerated VID_1209/PID_0001, opened CDC ACM COM25,
and recorded **256 bytes = eight exact 32-byte CLOG frames**.
Saved artifact: `../stage12-ep0fix-raw.bin`; watcher:
`../stage12-ep0fix-usb.log`. Hardware-positive image preserved at
`out/cactus/cactus-trace-stage-12-working-boot.img`.

The test proves arbitrary binary USB *transport* is possible from
freestanding shim code. The payload is a **fixed test frame**, not
kernel logs. TWRP/ADB did not return automatically in 44 seconds;
capture and restore/handoff have to be handled separately.
Older failure observations and untested hashes below are superseded
by this stage-12 proof.

## Current evidence and next raw-byte proof

**Stage 12 repeat after W1C IRQ fix:** Windows progressed from a failed
device-descriptor request to a failed **configuration-descriptor**
request. It still did not enumerate a COM port or receive a CLOG frame;
the phone did not expose TWRP ADB in the 40-second observation window.
This narrows the USB issue to the EP0 descriptor/control transfer path.

The next local build also requires an EP0 TX-completion interrupt before
sending the next packet of a multi-packet configuration descriptor.
That image is **offline-only** and has SHA256
`672772de175a8f3a51205f163f6de7ab8e0d6533efe54680e4d9afe7f7f8bbcf`.
It supersedes all Stage 12 hashes below. No claim of Windows USB
enumeration or raw log transfer is justified yet.

**Stage 13 is now hardware verified:** USB2 PHY activation changed
`DEVCTL` from `0x80` to `0x99` and produced endpoint-zero host
activity. It automatically returned to TWRP with a valid CTOK trace.
This confirms the original Stage 8/11 missing PHY initialization.

**Stage 12 test result:** Windows observed
`Unknown USB Device (Device Descriptor Request Failed)`; no CLOG bytes
were captured, and no automatic TWRP return was observed. We then
confirmed a second concrete defect in `musb_cdc_probe.c`:
`INTRUSB` and `INTRTX` status bits are **write-one-to-clear**,
per `cactus-4.9/drivers/misc/mediatek/usb20/mt6765/usb20.c`.
The original CDC loop left these bits set and could repeatedly process
the same USB RESET, wiping its state and USB address. The code is now
patched to acknowledge sampled IRQs before EP0 handling.

**Latest Stage 12 image:** SHA256
`6dbae7e9d68494ba9d4b9ee5804edabe9dbdf3acc295424412f79544014afedf`,
28,954,624 bytes, rebuilt offline and Android boot layout validated.
**This latest image has not been flashed or proven to enumerate.**
The earlier SHA values farther below document prior, superseded builds.

**Update after Stage 11 hardware test (21 October):** the stage
automatically returned to TWRP and its `CTOK` trace reported
`stage=11`, `status=1`, `state=11`, `HWVERS=e503`,
`iterations=748341` and `POWER=0x60` while connected.
However, **zero USB reset events, zero EP0 TX IRQs and zero RX-ready
samples** occurred. TWRP itself enumerates at high-speed on the same
device: `POWER=0x71`, `DEVCTL=0x99`. A USB2 PHY recovery step is
missing before attempting descriptors/CDC; flashing Stage 12 unchanged
would not distinguish that early link failure.

**Stage 13** `out/cactus/cactus-trace-stage-13-boot.img` is a new
two-second PHY recovery and interrupt-sampling test. It mirrors the
vendor cactus 4.9 `usb_phy_recover()`'s key PHY mode-bit operations,
including `set_usb_phy_mode(PHY_DEV_ACTIVE)`, then asserts MUSB
`SOFTCONN`, samples USB reset/EP0 IRQ/RX-ready activity and restores
the original PHY registers before returning to TWRP. It does not run
the vendor's complete clock and analog calibration sequence; absence
of USB events would still be inconclusive. Offline cross-build and
Android boot image checks passed, SHA256:
`06596288d65edc69607450bfd24e1802c30ef9049409be4c2599f8a9d6aec395`.
As with previous experiments, this PHY-register write path can hang
before TWRP handoff; its trace cannot survive a manual replacement.
The Stage 11 trace is at
`F:\ROM\patching\redmi 6a xnu\cactus-usb-stage11.bin`.

Real-device tests: **Stage 7, Stage 9, Stage 10 passed** and automatically
returned to TWRP with readable `CTOK` traces. Stage 8 flashed but Windows
did not enumerate its USB gadget, and TWRP return was not observed. Its
own CPIO trace could not be retrieved after manual recovery. Hence raw
USB logging from the shim is **not yet proven**.

Two new *offline-built* tests isolate the issue without recompiling Linux:

- **Stage 11** — `out/cactus/cactus-trace-stage-11-boot.img`.
  Uses the already-proven MUSB soft-connect sequence and spends two
  seconds reading only `INTRUSB`, `INTRTX`, `CSR0`, `DEVCTL`. It counts
  reset events, EP0 interrupts, and samples with RXPKTRDY, then
  disconnects and returns to the original TWRP kernel. It intentionally
  does not answer USB setup requests. This isolates whether interrupt
  and EP0 register reads explain Stage 8's failure. SHA256:
  `523b391a0f6ede8c041b8ed80ea772c733317c46d88e556d9d13a6af74e3db25`.
- **Stage 12** — `out/cactus/cactus-trace-stage-12-boot.img`.
  Ten-second CDC ACM experiment, returning to TWRP when it completes.
  It serves USB descriptors, implements CDC control requests, and
  queues a fixed **32-byte binary CLOG test frame** eight times *after*
  Windows asserts DTR on the COM port. The frame includes `0x00`,
  `0xff`, `0x7f`, `0x80`: an exact binary match on Windows proves that
  raw bytes, including non-text values, traversed USB bulk EP1.
  Guarded RXCOUNT and a bounded EP0 ack wait prevent known forms of
  runaway polling. SHA256:
  `1556a5c0b8e650b1c77657487a8356f38b65c728b0c3feb5536d57f3c3be162a`.

Both images pass local ARM64 cross compilation and Android v2 boot-image
layout checks. **Neither has been hardware-tested or flashed.** Test
Stage 11 *first*. Stage 12 still exercises experimental gadget setup,
which could prevent TWRP from automatically returning.

For the authorized Stage 12 test, start this Windows PowerShell 5.1
watcher **before rebooting**:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\capture-usb-raw.ps1" -Seconds 30
```

It detects the test VID:PID `1209:0001`, reports generic
`Unknown USB Device` errors (including failures before Windows gets
the descriptor), opens the corresponding `COMn` port, asserts DTR,
captures bytes without text decoding and compares them to the exact
known 32-byte frame. It writes
`F:\ROM\patching\redmi 6a xnu\cactus-usb-raw.bin` and prints
`PASS: exact 32-byte CLOG binary marker transferred over USB` **only
when the complete binary frame is captured**.

If the experiment automatically returns to TWRP, retrieve its own trace:

```cmd
adb pull /cactus-trace.bin "F:\ROM\patching\redmi 6a xnu\cactus-usb-stage11.bin"
python "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\trace_initramfs.py" decode "F:\ROM\patching\redmi 6a xnu\cactus-usb-stage11.bin"
```

For Stage 12 substitute `cactus-usb-stage12.bin`. Interpretation:
`resets>0` with `setups=0` points to missing EP0 traffic;
`setups>0` but `configured=0` points toward descriptor/control
handling; `configured=1` and `raw_frames_queued>0` means the gadget
attempted bulk transmission, **but only an exact Windows frame match
proves the bytes arrived**. A trace is recoverable only if the same
test image reaches TWRP. The Windows capture remains useful even if
the shim stalls.

The full XNU boot remains a separate milestone: a pre-handoff USB
test does not implement USB service after the kernel takes control;
XNU needs a compatible MUSB polling/logging driver and MMIO mappings.

The guide at `C:\Users\lapdq\Downloads\USB-SHIM-GUIDE.md` documents
Qualcomm's **Synopsys DWC3** gadget stack. Redmi 6A / cactus has a
**MediaTek MUSB** USB2 controller: DWC3 register offsets, TRBs, QSCRATCH
VBUS overrides and QMP PHY startup do **not** apply.

Verified on the attached Redmi 6A in TWRP 4.9.190+:

- `/sys/class/udc/musb-hdrc`, drivers `mt_usb` and `musb-hdrc`
- live DT `mediatek,mt6765-usb20` at `usb0@11200000`
- MUSB core: `0x11200000` (`0x10000` bytes)
- USB2 PHY DT window: `0x11cc0000`; PHY registers are `+0x800`
- interrupt SPI 73; clocks `usb0`, `usb0_clk_top_sel`,
  `usb0_clk_univpll3_d4`
- TWRP MUSB `HWVers=0xe503`, `POWER=0x71`,
  `DEVCTL=0x99`, `EPINFO=0x88`, `RAMINFO=0x8b`

Source of truth: `/home/lapdq/cactus-4.9/drivers/misc/mediatek/usb20/`,
especially `mtk_musb_reg.h`, `mt6765/usb20_phy.c`,
`mt6765/usb20.c` and `musb_gadget_ep0.c`. The previous Windows
device-connect sound shows electrical reconnection but is **not** proof
that Windows received a valid USB device descriptor.

## Built, separate stage probes

Build locally in Ubuntu WSL from `ios-cactus/shim`:

```sh
bash build_trace_probe.sh 7
bash build_trace_probe.sh 8
```

**Stage 7** `out/cactus/cactus-trace-stage-7-boot.img`: reads MUSB
`HWVERS`, `POWER`, `DEVCTL`, `EPINFO`, `RAMINFO`, interrupt mask and
five PHY registers into the `USB7` record, then chainloads the original
TWRP kernel. It makes *no USB MMIO writes*. The test passed on cactus
and produced a valid TWRP-return trace.

**Stage 8** `out/cactus/cactus-trace-stage-8-boot.img`: a separate,
experimental polling USB2 gadget in `musb_cdc_probe.c`. It checks the
MUSB revision `0xe503`, then tries to expose a temporary CDC-ACM
serial device for **10 seconds** before disconnecting and handing off
to TWRP. No DMA, DWC3 TRBs, XNU or persistent pmsg logging. It handles
USB standard EP0 setup/addresses/configuration and CDC ACM control
requests, uses EP1 for bulk data and EP2 for serial-state notifications.
It uses test VID:PID `1209:0001`, product `Cactus Shim USB`. Windows
10/11 should use its CDC ACM driver (`usbser.sys`) *if enumeration works*.
The bootloader must already leave a valid USB clock and PHY state;
the driver deliberately does not guess undocumented clock enables.
Stage 8 was hardware-tested and **did not enumerate or return to TWRP
automatically**. Stages 11 and 12 target this failure.

## Stage 8 hardware failure: split diagnostics

The Stage 8 device test did not enumerate in Windows and did not
return to TWRP without manual intervention. Since the trace is carried
inside Stage 8's own initramfs, booting a different TWRP image afterwards
cannot recover it. This is a failure of the observation method for hangs,
not evidence that MUSB registers are inaccessible: Stage 7 proved
read-only access in bare metal and returned to TWRP.

Builds added to isolate Stage 8 before another full enumeration attempt:

- **Stage 9** `out/cactus/cactus-trace-stage-9-boot.img`: calls the same
  freestanding C driver, reads MUSB identity and power and returns to
  recovery with a `USB8` record (`state=9`), without USB MMIO writes
  or a long poll loop. A successful test checks the C call boundary and
  read-only controller access, distinguishing it from the new write path.
- **Stage 10** `out/cactus/cactus-trace-stage-10-boot.img`: verifies the
  same identity, temporarily asserts MUSB USB2 soft connect for one
  second, then clears it and returns to recovery with `state=10`.
  This isolates basic MUSB writes and timer behavior from Stage 8's
  endpoint/CDC polling. It can still hang if these accesses are unsafe.

Both build locally with `bash build_trace_probe.sh 9` and
`bash build_trace_probe.sh 10`; check each resulting SHA and test in
that order. Only use the corresponding trace if that same boot returned
to TWRP. A manually flashed replacement cannot provide its trace.
The next persistent logging design must survive nonreturning hangs,
or use a proven watchdog chainload / real-time USB serial output.

Test **Stage 11 first** before attempting Stage 12's raw-binary transport.
The Redmi 6A's `fastboot boot IMAGE` has previously executed the
installed image instead; actual device testing uses a separately
authorized `fastboot flash boot IMAGE`.

After booting back to TWRP, with serial `3184a8a17d2b`:

```powershell
adb pull /cactus-trace.bin "F:\ROM\patching\redmi 6a xnu\cactus-usb-stage7.bin"
python "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\trace_initramfs.py" decode "F:\ROM\patching\redmi 6a xnu\cactus-usb-stage7.bin"
```

For a later separately authorized Stage 8 test, substitute
`cactus-usb-stage8.bin`; while its temporary USB device is present,
Windows PowerShell can inspect COM ports using:

```powershell
Get-PnpDevice -PresentOnly -Class Ports
```

Or start `powershell -ExecutionPolicy Bypass -File watch-usb-stage8.ps1`
before the test reboot. It polls for the temporary `1209:0001` device
throughout the ten-second window and reports when it appears/disappears.

The Stage 8 trace decoder reports the controller ID, USB resets,
`SET_ADDRESS` completions, setup packets, `SET_CONFIGURATION`, DTR/RTS
state and setup stalls. Zero setup packets suggests PHY/clocks/VBUS are
not ready. SETUP packets without configuration point to EP0/descriptors.

**Scope:** this isolates USB device enumeration from the XNU black-screen
hang. For live logs **after XNU owns the CPU**, XNU itself must drive MUSB
or retain a callable, correctly mapped service; a one-shot shim USB
loop stops when it branches to XNU. Similarly, the Stage 8 ten-second
experiment is not a general-purpose USB console.
