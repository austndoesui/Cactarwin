# Stage 14 / cactus variable binary USB trace

## Actual hardware pass: 2026-10-22

The corrected bulk endpoint TXCSR bit-0 image was subsequently flashed
and rebooted successfully on fastboot serial `3184a8a17d2b`, product
`cactus`. Windows enumerated COM25, received data with DTR asserted,
and reopened the port once during streaming.

Captured `../cactus-stage14-corrected-raw.bin`: **5,544 bytes**,
SHA256 `74b564feb44d246510696df54fa8947712db9e11dcd54c4684c5d2e1ea3c08dd`.
Decoder `--require-stage14` returned **STAGE14 PASS** with **99 valid
56-byte CLOG v2 records**, zero CRC errors, zero unmatched bytes,
zero duplicate or regressing sequences, and **10 missing sequence
numbers**. Events included startup, varying test values, hardware
identity and periodic records.

No TWRP ADB or fastboot device was visible at the read-only
post-session check; automatic recovery return remains unverified.
The original receiver threw an IOException closing an already-gone COM
device during shutdown; defensive cleanup was added afterward.
This proves the **pre-XNU binary USB logger transport** only. Next:
investigate sequence loss, reserve a safe ring with Benesse-style FDT
guards, and add entry/exception checkpoints to the real shim.

Stage 12 successfully enumerated Windows CDC COM25 and sent eight
fixed 32-byte CLOG frames. Stage 14 refactors the proven MUSB
controller into `cactus_usb.[ch]`, adds the `cactus_trace.[ch]`
32-entry ring of CRC-protected 56-byte CLOG v2 records, and emits
varying diagnostic events from `stage14_probe.c`. There is no
XNU boot or post-MMU USB support in this stage.

## Offline validation (completed)

- WSL arm64 build and Android boot v2 image validation PASS.
- Host C trace encoder tests PASS (wrap, overflow, CRC, ordering).
- Python stream decoder tests PASS (malformed CRC, noise, truncated
  frames, sequence gaps and variable binary payloads).
- Windows receiver PowerShell syntax PASS.

## Hardware attempts (2026-10-22)

- First image: flashed/rebooted successfully, Windows COM25 opened,
  deliberately closed and reopened; **0 bytes captured**.
- Second image: removed DTR gating; flashed/rebooted successfully,
  COM25 opened; **0 bytes captured**.
- Identified a concrete regression after these tests: Stage 14 was
  using **EP0 CSR0 TXPKTRDY bit 1** for EP1/EP2, but bulk/notification
  TXCSR uses **bit 0**. Corrected this in `cactus_usb.c` to match
  the hardware-positive Stage 12 sender.
- Corrected image now builds offline, but has **not been flashed**:
  `shim/out/cactus/cactus-trace-stage-14-boot.img`,
  size **28,956,672** bytes,
  SHA256 `24edb70a28724f93e46d43e8f53e037f73c96f81c228ceaa1976a16d562045e4`.
- Fastboot was absent after the previous hardware run. Device
  recovery is required before testing the corrected image.

## Next hardware test

Start the Windows receiver *before* reboot using a terminal:

~~~powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
powershell -NoProfile -ExecutionPolicy Bypass -File "$root\ios-cactus\shim\capture-cactus-trace.ps1" -Seconds 45 -Output "$root\cactus-stage14-final-raw.bin"
~~~

In another terminal, once cactus is in fastboot:

~~~powershell
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\out\cactus\cactus-trace-stage-14-boot.img"
fastboot -s 3184a8a17d2b reboot
~~~

Decode the saved host stream:

~~~powershell
python "$root\ios-cactus\shim\decode-cactus-usb.py" "$root\cactus-stage14-final-raw.bin" --require-stage14
~~~

**Pass requires 20+ distinct valid CLOG v2 frames** with CRC,
monotonic sequence numbers and variable event data. Report missing
sequence numbers, DTR and recovery status independently. If the same
boot returns to TWRP, pull `/cactus-trace.bin` and use
`trace_initramfs.py decode` for Stage 14 device counters. The old
working Stage 12 image remains preserved unchanged.
