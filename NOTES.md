# Cactus XNU working notes

**Updated:** 2026-10-23. Device: Xiaomi Redmi 6A (`cactus`, MT6761/MT6765). Current Git branch before consolidation: `cactus-phase1`. The firmware and boot images are experimental; **XNU has not booted an OS**.

## Verified current state

- Stock LK boots the Android header-v2 shim; the shim enters XNU at EL1, with MMU enabled during verified checkpoints.
- Live MUSB CDC raw USB logging works through the host's serial port. Binary records have passed CRC, frame order and completeness checks. See [USB logging plan](XNU-USB-LOGGING-PLAN.md).
- Stage 22/build 27 reached a deliberate deeper XNU C checkpoint (54 valid frames).
- Stage 23/build 28 removed that artificial checkpoint; the first genuine XNU synchronous fault reached `_DebuggerTrapWithState`.
- Stage 24/build 29 captured saved XNU registers and resolved the caller to `_pmap_map_bd.cold.1` (two clean hardware captures: 68 and 70 frames).
- **Stage 25/build 30 on hardware PASS**, October 23: captured 77 CRC-valid, complete frames, with zero bad frames or sequence gaps. Verifier: `STAGE25 NULL-PTE REQUEST CAPTURED; COM25 HELD`. A second partial session in the same run is **not** another Stage25 pass.

## Current XNU boot blocker — hardware measured

XNU `pmap_map_bd()` tries to map the display framebuffer across a kernel 1 GiB VA boundary:

| Value | Verified Stage 25 capture |
| --- | --- |
| Physical framebuffer start | `0x5e605000` |
| Exclusive physical end | `0x5e9fa000` |
| Initial requested virtual address | `0xfffffff049dbb000` |
| First failing virtual address | **`0xfffffff04a000000`** |
| Active TTBR1 | `0x824c1000` |
| Missing L1 descriptor slot | **`0x824c1e08`, value `0x0`** |

The immediate issue is the missing early kernel pmap subtree when `io_map` reaches the next VA region. The source-area lead is `osfmk/arm64/arm_vm_init.c` near `va_l1_end` (local XNU sources, not necessarily stored in this Git checkout). Investigate real startup allocation bounds and build **proper** L1/L2/L3 descriptors. Do not fake a descriptor, NOP a debugger trap or skip the panic.

The user sees the **MI logo** during COM25, but did **not** see the Stage 23 three-color test pattern. Framebuffer RAM readback alone does not demonstrate active scanout.

## Most recent proven build and logs

- Image, locally generated and Git-ignored: `shim/out/cactus/cactus-trace-stage-30-boot.img` (45,506,560 bytes).
- SHA-256: `65efa3fdbc9b5e28ca7e3298f9a5ca794939852ebf57a93001f2287d339b155b`.
- Hardware capture remains **outside Git**: `../usb-captures/stage-cactus-20261023-212828-797/session-001-212942-364.bin`.
- Decode it: `python shim/decode-cactus-usb.py "CAPTURE.bin" --require-stage25 --show 90`.
- [Complete event-level proof](shim/TRACE-STAGE25.md) and [historical Cactus notes](docs/CACTUS-BRINGUP-HISTORY.md).

## Next action: Stage 26 (planned, not built)

1. Correct XNU's early virtual space and page-table coverage through the framebuffer mapping (validate against hardware geometry and the memory layout).
2. Rebuild the real XNU Mach-O payload, repack the Android header-v2 shim, and preserve a first-exception COM25 logger.
3. Test in fastboot only once a verified image is prepared. The success gate is XNU execution past the prior `pmap_map_bd` panic. Continue diagnostics for subsequent faults.
4. Separately verify LK display scanout and add a minimal on-screen diagnostic marker/console after a visible framebuffer test succeeds.

## Workflow reminders

Windows PowerShell + WSL Ubuntu is the tested host. Many packaging prerequisites and build outputs are **local, ignored files**. `./shim/build_trace_probe.sh 30` is a local WSL shim-image rebuild, while the XNU source itself uses the numbered patch series and `machine_config=CACTUS` in the GitHub workflow. The build script is not a fresh-clone, zero-configuration build.

This repo descended from an older SWEET/Qualcomm port; filenames of source patches and utilities may retain that lineage. Historical SWEET-specific status, UFS assumptions and unrelated iOS GUI claims are excluded from the current Cactus handoff. Never treat the inherited project's results as Cactus hardware evidence.
