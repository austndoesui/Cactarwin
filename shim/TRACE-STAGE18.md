# Stage 18 — hardware-derived XNU payload admission with USB tracing

**2026-10-22: original Stage18 was flashed and diagnosed on hardware.
Aligned Stage18 rebuilt and validated offline; the aligned build has not
yet been flashed.**

## First Stage18 hardware result and corrected build

The original Stage18 image was flashed to verified cactus on 2026-10-22.
Two separate USB recorders collected five CRC-valid identical boot
sequences (10 frames / 560 bytes each, no sequence gaps), from
`../usb-captures/stage-cactus-20261022-155400-247` and
`../usb-captures/stage-cactus-20261022-155424-470`.
The user's original text log is
`C:\Users\lapdq\OneDrive\Documents\NoirUI_PteroTheme\stage18latestdump.txt`.

Every attempt reached USB initialization (`0x1801`), successful FDT
parse (`0x1811`, 1 bank, 17 reservations), the copied SWEETXNU
payload header (`0x1820`, 10 segments, 480 MiB), the live FDT free
window check (`0x1821 a=1`), and the pre-validator checkpoint
(`0x1830`). It then entered the CPU exception logger
(`0x18ee`) with `ESR_EL2=0x96000021`,
`ELR_EL2=0x41ec4174`, `FAR_EL2=0x52bcbfdc`.

Cross-checking the AArch64 disassembly showed the faulting instruction
is `ldr x9, [x0,#56]` in `xnu_validate_payload`. This reads the
payload `phys_base` field: the header starts at `0x52bcbfa4`,
and `0x52bcbfa4 + 0x38 = 0x52bcbfdc`. The source pointer has only
four-byte alignment, so this is a **reproducible 64-bit alignment
fault during diagnostic payload reading**. It stopped before the
admission verdict (`0x1831`) or TWRP chainload; the exception handler
does not resume execution. It did **not** enter XNU. The recurring
COM25 disconnection/reset mechanism remains unproven.

The corrected build changes the CPIO filename padding so the embedded
payload starts at `0x52bcbfb0` (16-byte aligned), adds a build-time
alignment assertion and a runtime pointer alignment gate, and shortens
USB polling from 11.5 to 5.5 seconds to attempt returning to TWRP
earlier. The original flashed image and its SHA remain available for
reproduction.

**Corrected image, not yet hardware-tested:**
`out/cactus/cactus-trace-stage-18-aligned-boot.img`

- Size: **45,502,464 bytes**
- SHA256:
  `ead3617f6738b7a019da421826a8ad6f85c16a1297404645ed2659c8445cb152`
- Verified physical payload pointer `0x52bcbfb0` mod 16 = 0.
- Embedded 16,540,608-byte payload matches the generated source copy
  byte for byte (SHA256
  `f1cbeff59113e27a66e4ed70e4b2b59c5318aa36825ac0d961040f7ae3bb5b1f`).
- ARM64 build, Android boot v2 validation, seven USB decoder tests,
  and the real Stage17 17-reservation C admission test all PASS.

The next hardware test should use this **aligned** image and look
for `0x1831`, all ten `0x1840` records, `0x18f0`, and `0x18ff`.
Successful diagnostic completion would still only prove the payload
layout was admitted; XNU execution is a separate milestone.

## What changed

Stage17 established that LK passes a 2 GiB RAM bank
`[0x40000000,0xc0000000)` and 17 reserved intervals on the actual
Redmi 6A. The existing XNU payload requested
`[0x70000000,0xc0000000)` and overlaps those reservations.

Stage18 creates a **copy** of the original SWEETXNU payload, changing
its physical RAM window to `[0x80000000,0x9e000000)` (480 MiB) and
updating the Apple ADT `/chosen/dram-base` and `dram-size` properties
to match. The source `out/cactus/payload.bin` is unchanged.

The preflight links the very same `xnu_validate_payload()` function used
by the normal cactus loader. On the device it reads the live FDT,
checks every one of its memory/reservation ranges, verifies payload
headers, Mach-O segment intervals, entry address and boot_args space,
then reports calculated physical addresses through polled MUSB CDC
CLOG v2 records. The image retains the working Stage14–17 recovery
wrapper and TWRP chainload, and emits DONE promptly before the longer
USB polling period.

**Stage18 does not copy XNU segments, enable the MMU, enter EL1 or run
any XNU instructions.** The 480 MiB window is an *admission-test*
candidate, not proof that XNU's memory manager can boot with it.
No LK changes, firmware writes or userdata changes are involved.

The **normal** cactus loader now also calls this fail-closed validator
on every `BOARD=cactus` build, including `STAGE15=0`, before copying
XNU segments. This is independent of the Stage18 USB-only probe.

## Output and offline verification

Built via:

```bash
cd '/mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim'
./build_trace_probe.sh 18
```

- First hardware-tested Stage18 image:
  `out/cactus/cactus-trace-stage-18-boot.img` (**superseded; contains
  the proven unaligned payload fault**). Use the corrected `-aligned-`
  file above for the next test.
- Size: **45,502,464 bytes**, fits the reported 64 MiB boot partition.
- SHA256:
  `6231e0cc6a56414f1664c003f0752e66929f9b1cc7b1bd29bc65e7cc98f3b8d2`
- Android boot v2 structure validator: PASS.
- Appended recovery ramdisk contains an independent payload CPIO entry;
  payload pointer `0x52bcbfa4`, payload bytes `16,540,608`.
- Confirmed ARM64 symbols: `stage18_probe`, `stage18_fault`,
  `fdt_inspect`, `xnu_validate_payload` and `image_end`.
- Local CLOG decoder: seven regression tests PASS.
- Native host validation using **all 17 actual Stage17 reservations**:
  original payload rejected (`XNU_VALIDATE_UNSAFE_RAM=4`);
  relocated candidate admitted (`XNU_VALIDATE_OK=0`).
  Calculated entryPA `0x8142d488`, boot_args `0x824bc000`.
  Corrupted segment offset and reserved-window negative checks PASS.
- Separately crossbuilt the normal cactus loader with shared validator
  (`shim-stage18-loader-check-boot.img`, offline build only).

## Binary events (CLOG stage = 18)

| Event | Payload |
| --- | --- |
| `0x1800` | entry: FDT pointer, CurrentEL, counter frequency |
| `0x1801` | MUSB initialized: success, HWVERS, power |
| `0x1810` | FDT header: valid, FDT address, bytes |
| `0x1811` | FDT parsed: valid, memory banks, reserved count |
| `0x1820` | relocated payload: base, size, segment count |
| `0x1821` | FDT range free flag, copied payload location/size |
| `0x1830` | just before validator; payload pointer, shim base, shim size |
| `0x1831` | **admission verdict** (0 = safe layout), base, size |
| `0x1840` | segment index, calculated destination PA, segment bytes |
| `0x18f0` | safe-layout result: calculated entry PA, boot_args PA, top |
| `0x18ff` | diagnostic DONE: verdict, pending, dropped frames |
| `0x1802` | changing heartbeat during USB polling |
| `0x18ee` | exception: ESR, ELR, FAR if USB survives |

Admission success requires the strict decoder `--require-stage18`
with successful FDT/USB, verdict 0, every segment, READY and DONE.
If the gate fails, the last transmitted event tells which step was
reached; do not mistake host disconnection for an XNU fault.

## Corrected build hardware test, once fastboot is connected

Start the reconnecting Windows USB watcher in one PowerShell window
**before reboot**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\watch-cactus-usb.ps1' -Seconds 120
```

Verify the serial/product and flash the Stage18 diagnostic from another:

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot "$root\ios-cactus\shim\out\cactus\cactus-trace-stage-18-aligned-boot.img"
fastboot -s 3184a8a17d2b reboot
```

Inspect **each** session binary saved by the watcher (session filenames
are unique and may span several boot/USB reconnect attempts):

```powershell
$root = 'F:\ROM\patching\redmi 6a xnu'
Get-ChildItem "$root\usb-captures" -Recurse -Filter 'session-*.bin' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 5 |
    ForEach-Object {
        python "$root\ios-cactus\shim\decode-cactus-usb.py" $_.FullName --show 55 --require-stage18
    }
```

**Next milestone if admitted on hardware:** a separate Stage19 probe
that logs a bounded *real* XNU loader segment copy and EL2→EL1 handoff,
then a verified first instruction inside the XNU payload. USB must be
kept alive/recorded across the handoff before a claim that XNU boots.
