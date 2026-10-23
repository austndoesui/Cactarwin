# Stage 21-C1 / hardware Stage24: pre-MMU EL1t page-table gate

Status: **initial Stage24 flashed 2026-10-23 and returned to TWRP, but did
not enumerate COM25 or produce a CLOG capture; revised Stage24 built and
checked offline, awaiting hardware USB capture.** Build and code checks
are not an on-device pass.

Image: `shim/out/cactus/cactus-trace-stage-24-boot.img`

- Size: **45,498,368 bytes**
- SHA256 of **revised, unflashed** image:
  `e348d65b798e1a45112df5ded867a830d67f099b032a3784802178fe9a0fbbe7`
- SHA256 of the initial unsuccessful-on-COM Stage24 image:
  `9a8721e0a89cdb381fabbded0ccebb40b183c5d599b7153d39823155f3fb010f`
- Android boot header v2 and ramdisk image validated by `check_image.py`.
- Baseline Stage20-D stage23 image SHA256 unchanged:
  `15acded79146d78971436bee7ac2fe14c3c6f7dcf3cbd4854798466f616f281c`.

The known XNU payload is the Stage20-D SWEETXNU image pinned by
`check_stage21b_mmu.py` and `check_stage21c1_entry.py`. The latter
rejects altered opcodes or altered original EL1t vector words.
The Stage24 runtime also checks the 8 translation-path instructions,
boot arguments, TCR constant and original physical BRK target before
patching the loaded XNU image. It preserves the full XNU framebuffer
memory-writing path from Stage20-C.

## Exception path and why it is stackless

XNU has installed its physical exception vector at PA `0x8142e000`,
set `SP_EL1` and `SP_EL0` to **virtual addresses**, and switched to
**EL1t (SP_EL0)**. It has not yet enabled the MMU.

The instrumentation patches the XNU instruction at physical
`0x8142f474` (`msr vbar_el1,x0`) to `BRK #0xe21` and replaces the
**EL1t synchronous vector at PA `0x8142e000`** with a temporary
`HVC #0x21` instruction (`0xd4000422`).

On the BRK, the CPU vectors to `0x8142e000`. HVC takes execution to
EL2 immediately, **without using XNU's virtual EL1 stacks**. EL2
reads the exception registers from EL1, samples XNU's physical table
descriptors, and sends records through the previously proven USB
logger. The existing EL2 return trampoline restores x19–x30 and
returns to the Linux/TWRP recovery chain. Before return, the shim
restores the original instruction and four vector words.

**Correction to the initial Stage24 image:** entry to the EL1t
synchronous vector switches exception execution to **EL1h**; therefore
the nested HVC captures **EL1h (mode 5)** in `SPSR_EL2`, while
`SPSR_EL1` preserves interrupted **EL1t (mode 4)**. The earlier
runtime/strict parser mistakenly expected mode 4 in `SPSR_EL2`.
The revised image checks both modes separately.

**USB troubleshooting:** the first attempt's Windows PnP record showed
only Xiaomi/ADB (`VID_2717&PID_FF48`), never shim CDC
(`VID_1209&PID_0001`). A TWRP pull of `/cactus-trace.bin` found the
initial `CTRC` header (stage=0), **not** the `CTOK` completion marker.
This does not isolate the failing instruction. Previously there was no
long USB polling window before the XNU jump: if execution stopped
there, Windows could miss all shim frames. The revised Stage24 emits
event `0x21ca` and polls USB for **5.5 seconds before ERET to XNU**.
That makes a partial preflight capture useful even if EL1t hangs; any
missing final records still fail the strict hardware gate.

## Runtime evidence required

All events are 56-byte CRC-protected CLOG frames with stage byte 24:

| Event | Expected result |
| --- | --- |
| `0x21c6` | Original EL1t vector was verified and patched at `0x8142e000`; direct `HVC #0x21` opcode `0xd4000422`. |
| `0x21c0` | XNU entry `0x8142d488`; temporary BRK target `0x8142f474`; branch opcode `0x14000719`. |
| `0x21ca` | Pre-ERET USB handoff marker after successful segment copy and reversible patch, before a 5.5 s USB enumeration/drain interval. |
| `0x20a1` | Real EL1 entry from Stage20. |
| `0x21c1` | EL2 reads EL1 BRK syndrome (EC=0x3c, immediate=0xe21), `ELR_EL1=0x8142f474`, FAR. |
| `0x21c2` | `VBAR_EL1=0x8142e000`, `TCR_EL1=0x22a5192519`, `SCTLR_EL1=0x30d00800` (M=0). |
| `0x21c3` | `TTBR0_EL1=0x8117a000`, `TTBR1_EL1=0x8117b000`, `MAIR_EL1=0x0c08044f00bb44ff`. |
| `0x21c4` | Physical TTBR0 root and high-VA / framebuffer TTBR1 roots, respectively: `0x8117c003`, `0x8117d003`, `0x8117e003`. |
| `0x21c7` | First physical and high-VA 2MiB blocks `0x81000601` each; first Normal-NC framebuffer alias block `0x6000005e600605`. |
| `0x21c8` | Second framebuffer block `0x6000005e800605`; table slot `0xef=0` and preceding mapped slot `0xee=0x9dc00601`, exposing an apparent final-2MiB gap. |
| `0x21c9` | `ELR_EL2=0x8142e004` after vector HVC, `SPSR_EL2` mode EL1h (5), and independently sampled `SPSR_EL1` mode EL1t (4). |
| `0x20b3`, `0x20a3`, `0x21c5`, `0x20ff`, `0x19ff` | EL2 HVC, resume, both patches restored, zero-status recovery. |

The successful parser verdict is **`STAGE21-C1 PRE-MMU EL1t
PAGE-TABLE GATE PASS`**. Any missing/misordered, invalid, incorrect
EL-mode, CRC-broken or truncated trace is a failure. The snapshot
records intentional bootstrap mappings only; normal XNU memory
management and actual LCD output remain future stages.

## Local reproduction / eventual hardware capture

Build in WSL Ubuntu, from `ios-cactus/shim`:

```sh
./build_trace_probe.sh 24
python3 -m unittest -v test_stage21c1_entry test_stage21c1_usb \
    test_stage21b_mmu test_stage20d_entry test_stage20d_usb
```

When the device is verified to be in **fastboot** and user permission
applies, start the existing `watch-cactus-usb.ps1` watcher on Windows
first. Then:

```powershell
fastboot -s <cactus-fastboot-serial> getvar product
fastboot -s <cactus-fastboot-serial> flash boot 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\out\cactus\cactus-trace-stage-24-boot.img'
fastboot -s <cactus-fastboot-serial> reboot
```

Run the strict decoder against the **actual session BIN** saved by
the USB watcher (the console can remain black):

```powershell
python 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\decode-cactus-usb.py' '<capture-session.bin>' --require-stage21c1 --show 70
```

Only declare hardware success after the strict decoder passes and
TWRP is observed again. Stage21-C2 (MMU-on checkpoint) must not be
treated as tested by this pre-MMU image.
