# Cactus bring-up chronology (historical archive)

This is a chronological record of earlier Cactus Stage 1–25 experiments.
Some lines represent observations or plans that were later superseded.
For the current validated blocker, use [NOTES.md](../NOTES.md) and
[Stage 25 hardware trace](../shim/TRACE-STAGE25.md).

# Historical Cactus XNU bring-up working notes

## CACTUS Phase 1 (Redmi 6A, MT6765) - active 2026-10-20

### 2026-10-23 Stage25 build30 hardware PASS: missing framebuffer L1 mapping

- Authorized fastboot `flash boot` and `reboot`: both OKAY on cactus.
  COM25 first complete Stage30 capture:
  `usb-captures/stage-cactus-20261023-212828-797/session-001-212942-364.bin`,
  **77 CRC-valid frames, zero errors**, strict `--require-stage25` PASS.
- **CONFIRMED ROOT BLOCKER:** `pmap_map_bd` was trying to map framebuffer
  PA `0x5e605000..0x5e9fa000` at VA beginning
  `0xfffffff049dbb000`, but at **VA `0xfffffff04a000000`** the
  TTBR1 (`0x824c1000`) L1 descriptor slot **`0x824c1e08` = zero**.
  This is the 1 GiB VA table boundary absent from early kernel pmap.
  Hardware no longer requires guessing which IO mapping causes panic.
- Investigate and fix early VA/PTE coverage in local XNU
  `/home/<local-user>/xnu-cactus/osfmk/arm64/arm_vm_init.c` around
  `va_l1_end` computation lines 2334-2350. Allocate real descriptors
  through framebuffer extent and preceding I/O allocations; validate
  against all kernel space limits, then rebuild XNU payload/shim.
  DO NOT NOP debugger trap or insert random descriptor addresses.
- Device not detected in fastboot during post-capture check. The
  Stage30 image holds COM25; use hardware fastboot entry then
  `fastboot oem reboot-recovery`. See `shim/TRACE-STAGE25.md`.

### 2026-10-23 Stage25 / build30: null-PTE capture image offline PASS

- Added reversible `BRK #0xe25` at the **exact** null-PTE panic call in
  XNU `_pmap_map_bd` (PA `0x81405218`, VA `0xfffffff007405218`).
  Live shim opcode guards and offline `check_stage25_pmap.py` verify
  the original BL opcode `0x941ae25e` and the function's saved-reg
  prologue before insertion. x19 = failing VA, x20 = initial VA,
  x21 = physical end, x22 = physical start. The BRK is restored after
  the EL2 exception. No guessed memory mappings or nop panic fixes.
- Stage30 COM25 `0x2500..0x2508` report original call opcode,
  register-based VA/PA request, bounded TTBR1 L1/L2/L3 page walk,
  and code restoration. Decoder `--require-stage25` is strict and
  prints missing virtual address and physical range directly.
- `shim/out/cactus/cactus-trace-stage-30-boot.img` local WSL build PASS,
  45,506,560 bytes SHA256
  `65efa3fdbc9b5e28ca7e3298f9a5ca794939852ebf57a93001f2287d339b155b`.
  Android boot-v2 structure, pmap byte guards, and six targeted
  decoder/entry regression tests PASS. See `shim/TRACE-STAGE25.md`.
- **Hardware test PENDING:** most recent `fastboot devices -l` returned
  no devices and Windows PresentOnly PnP had no connected cactus entry.
  ADB's daemon also failed to initialize in the local sandbox.
  Earlier in this session the phone was detected in fastboot, but the
  latest check cannot confirm a connection. No Stage30 flash performed.
  Exact failing VA and PA remain unknown until a Stage30 COM25 dump.

### 2026-10-23 Stage24 / build29 hardware panic source verified

- Two approved fastboot Stage29 boot tests. Captures:
  `usb-captures/stage-cactus-20261023-145638-471/session-001-145806-881.bin`
  (68 CRC-valid frames) and
  `usb-captures/stage-cactus-20261023-150509-022/session-001-150557-967.bin`
  (70 CRC-valid frames), strict `--require-stage24` PASS, no corruption.
- **Original boot panic source**: `_pmap_map_bd.cold.1+0x24` in XNU;
  runtime pointers resolve to strings `panic` and `pmap_map_bd @%s:%d`.
  The debugger's `0xe7ffdeff` is only the panic trap, not the cause.
  Local XNU source `/home/<local-user>/xnu-cactus/osfmk/arm/pmap/pmap.c`
  lines 1883-1886 panics if `pmap_pte(kernel_pmap,vaddr)` is NULL.
  Early call path `/home/<local-user>/xnu-cactus/osfmk/arm/io_map.c` calls
  `pmap_map_bd` while assigning I/O VA from `virtual_space_start`.
  **Exact failing VA / device PA unknown**, need next probe/fix for
  early I/O page-table coverage; do not NOP panic trap.
- Final Stage29 image `shim/out/cactus/cactus-trace-stage-29-boot.img`,
  45,506,560 bytes SHA256
  `6ee308d9407a159200e2dad3cc826fbfc3c56f8843b08b882176df70bc609530`.
  Added guarded direct `__BOOTDATA` physical stack lookup; one saved
  frame read but previous FP/LR were zero, so no full unwind.
- **Latest device status:** Stage29 USB diagnostic hold completed, device
  did not enumerate in ADB or fastboot during a bounded recovery poll.
  User must enter fastboot via buttons; from fastboot use
  `fastboot oem reboot-recovery` (without flashing recovery).
  See `shim/TRACE-STAGE24.md`. The full XNU boot and screen rendering
  remain unverified; user saw only the MI logo.

### 2026-10-23 Stage24 / build Stage29 — offline PASS, hardware pending

- Stage28's `ESR_EL1=0x02000000`, `ELR_EL1=0xfffffff0072a9e64`
  maps via the **actual packed kernel Mach-O `LC_SYMTAB`** to
  `_DebuggerTrapWithState +0x84`, opcode `0xe7ffdeff` (undefined).
  This is a debugger/panic path; don't replace it with NOP. Need the
  **earlier** caller and panic cause, not a guessed instruction fix.
- Added `shim/inspect_stage24_fault.py`: inspects the embedded XNU
  symbol table, and optionally symbolicates COM25 register/stack events.
- Stage29 preserves Stage28 XNU execution and USB/HVC fault hold. The
  EL2 assembly handler captures original XNU x0..x8, x19..x30,
  SP_EL0/SP_EL1 before clobber; C emits `0x2400..0x2407`, then
  bounded TTBR1 4KB page walk of x29 saved-frame chain (0x2410..0x2413,
  summary 0x241f). Never follows addresses outside measured DRAM.
- WSL local `./build_trace_probe.sh 29` **PASS**, Android boot-v2
  structure **PASS**, 4 targeted Stage24/23/22/21D decoder tests PASS.
  Artifact: `shim/out/cactus/cactus-trace-stage-29-boot.img`,
  **45,506,560 bytes**, SHA256
  `54eea2e7abf3917047e21662ca7ef33e08ddaac73d7c6131129054bc2a17bf9e`.
  Decode with `--require-stage24`, symbolize with
  `shim/inspect_stage24_fault.py --capture`.
  See `shim/TRACE-STAGE24.md`. **Not flashed or hardware-tested.**
- User confirmed **no colored Stage28 framebuffer marker was visible**,
  only the MI splash. RAM readback is not evidence of active LK scanout.

### 2026-10-23 Stage23 / Stage28 hardware first XNU exception captured

- Stage28 boot flash and reboot succeeded. Independent COM25 watcher
  captured 3 sessions; first two stopped after the framebuffer RAM
  sample. The third:
  `usb-captures/stage-cactus-20261023-142123-475/session-003-142252-861.bin`,
  **58 CRC-valid frames**, strict `--require-stage23` =>
  **STAGE23 GENUINE XNU FAULT CAPTURED; COM25 HELD**.
- Native XNU entered EL1 then raised ESR_EL1=`0x02000000` (EC=0),
  ELR_EL1=`0xfffffff0072a9e64`, FAR_EL1=0. MMU enabled;
  TTBR0=`0x824c0000` TTBR1=`0x824c1000` (advanced from Stage22).
  EL2 HVC reached; vectors restored `(1,1,1)`, hold marker `0x2308`,
  completion statuses zero. Resolve first real exception PC against
  exact payload next; do not infer fully booted XNU or working IRQ/SMP.
- Three-color framebuffer pixel readback PASS, but visible screen
  pattern needs the user's observation. Fastboot after the third run;
  `fastboot oem reboot-recovery` OKAY and ADB confirmed Redmi 6A TWRP.
  See `shim/TRACE-STAGE23.md`.

### 2026-10-23 Stage23 / build Stage28 — offline PASS, device test pending

- User reports **MI logo visible during COM25**. Treat display/backlight
  as physically active; test whether our known `0x5e605000` boot_args
  framebuffer is actually the active LK scanout. Stage28 writes a bounded
  288x80 three-color marker (x=48..335, y=80..159), cleans data cache,
  and logs memory pixel samples (`0x2301`,`0x2302`). A visible marker
  has *not* yet been confirmed; readback alone cannot show display scanout.
- Stage28 preserves hardware-proven Stage27 payload validation, early
  high-VA XNU entry, EL1t+EL1h synchronous vector HVC-to-EL2 logging.
  It **does not install Stage27's synthetic BRK #0xe24**, so XNU may
  execute beyond `0xfffffff007ac0c0c`. On a captured real synchronous
  fault it logs ESR/ELR/FAR and translation registers, restores vectors,
  then holds USB indefinitely instead of deliberately chainloading
  recovery. Hangs/SError/external watchdog resets are still possible.
- WSL local `./build_trace_probe.sh 28` **PASS**, Android boot-v2
  header verified. `shim/out/cactus/cactus-trace-stage-28-boot.img`
  **45,506,560 bytes**, SHA256
  `939ab0864e06fb1c5d7bb6bdc53fdeacdb4dc3892b58c1db2f4cff31ee588e93`.
  Stage23+Stage22+Stage21D decoder/guard tests: **5 PASS**.
  ELF calls `stage19_probe` and `stage20_el1_transition` and includes
  data cache cleaning. See `shim/TRACE-STAGE23.md`; USB decoder
  `--require-stage23` differentiates full fault/hold from unconfirmed
  handoff. **Device not flashed.**

### 2026-10-23 Combined Stage22 / Stage27 hardware checkpoint PASS

- Stage27 user-authorized fastboot flash and reboot both returned OKAY.
  COM25 capture `usb-captures/stage-cactus-20261023-134531-452/session-001-134634-496.bin`:
  3,024 bytes, **54 CRC-valid CLOG frames**, zero gaps/errors. Strict
  `--require-stage22` => **STAGE22 DEEPER XNU C CHECKPOINT PASS**.
  ESR_EL1=`0xf2000e24`, ELR_EL1=`0xfffffff007ac0c0c`,
  SCTLR_EL1=`0x34d4593d`; CPU MIDR=`0x410fd034`, MPIDR=`0x80000000`,
  CNTFRQ=13,000,000 Hz. Patches restored `(1,1,1)` and return codes 0.
- After test fastboot enumerated serial `<redacted-device-serial>`;
  `fastboot oem reboot-recovery` returned OKAY and ADB confirmed
  `recovery product:omni_cactus model:Redmi_6A device:cactus`.

- Local WSL build `shim/build_trace_probe.sh 27` succeeded. Boot-v2
  verification PASS; output `shim/out/cactus/cactus-trace-stage-27-boot.img`,
  **45,504,512 bytes**, SHA256
  `5422a4f2dc6fe0f18cafd88127a4e54f270a32342e9cdb0926a6c014a3151ea2`.
- Advances XNU from verified Stage26 high-VA C prologue through several
  early calls to a guarded later BRK #0xe24 at VA
  `0xfffffff007ac0c0c` (PA `0x81ac0c0c`). Real earlier synchronous
  faults enter the proven dual virtual-vector HVC to EL2 USB logger.
  All temporary patches restored on the EL2 return path.
- Added read-only CPU MIDR/MPIDR, timer counters/frequency and loaded
  boot memory census to preflight; no GIC, SMP, timer IRQ configuration
  or device hardware claim before real hardware testing.
- Static payload guard + synthetic USB positive/fault/corruption checks,
  plus Stage25/Stage26 regression: **6 tests PASS**. Linked Stage27 ELF
  explicitly calls `stage19_probe` and `stage20_el1_transition`.
  Decoder `--require-stage22`; docs `shim/TRACE-STAGE22.md`.
- **Stage27 has now passed on hardware**, advancing demonstrated XNU
  high-VA C execution to `0xfffffff007ac0c0c`. Timer interrupts, GIC,
  SMP and full XNU boot are still unverified.

### 2026-10-23 Stage21-D / Stage26 high-VA hardware PASS

- User-authorized fastboot flash returned OKAY, product `cactus`; reboot
  returned OKAY. COM25 capture:
  `usb-captures/stage-cactus-20261023-130759-533/session-001-130859-702.bin`.
  Strict Stage21-D checker: **HIGH-VA C CHECKPOINT PASS**, 51 CRC-valid
  frames, zero gaps. ELR_EL1 `0xfffffff007ac061c` (BRK #0xe23),
  SCTLR_EL1 `0x34d4593d`, patches restored, USB logger returned via EL2.
- Multiple complete COM25 sessions observed during repeated rebooting.
  Later fastboot recognized serial `<redacted-device-serial>`;
  `fastboot oem reboot-recovery` returned OKAY and ADB confirmed
  `recovery product:omni_cactus device:cactus`. **TWRP confirmed.**

- New `shim/out/cactus/cactus-trace-stage-26-boot.img`,
  45,504,512 bytes, SHA256
  `af28b4019764e7d55ede1c83c96f88ac40fb568f886f1f588affde915dc5a09a`.
  Local WSL build completed, Android boot-v2 image structure verified.
- Exact-payload guard now pins the first high-VA XNU C prologue at
  `0xfffffff007ac05dc`, including stack saves and memory ops.
  BRK #0xe23 at VA `0xfffffff007ac061c`, PA `0x81ac061c`.
  Both mapped synchronous vector slots (EL1t + EL1h) route directly
  to the working EL2 physical-stack COM25 logger; handler reports
  the first exception and classification before restoring patches.
- Strict parser `--require-stage21d` reports either a high-VA
  checkpoint or an earlier XNU exception, with CRC/order/return
  checks. **Six Stage21 targeted tests passed**, including Stage25
  regressions and deliberate corruption. Linked ELF contains the
  real `stage19_probe` and `stage20_el1_transition` calls.
- Stage26 hardware test passed above; see `shim/TRACE-STAGE21-D.md`.

### 2026-10-23 Stage21-C2 / Stage25 hardware PASS

- Corrected Stage25 boot image
  `shim/out/cactus/cactus-trace-stage-25-boot.img`,
  45,504,512 bytes, SHA256
  `489f6624e18ceca84d1f347bbf4c2cef6946fbde263e55b11410ff7f301012c5`.
  Flashed to verified Redmi 6A `<redacted-device-serial>`, `product: cactus`,
  using elevated fastboot; send, write and reboot returned OKAY.
- Hardware COM25 capture:
  `../usb-captures/stage-cactus-20261023-121212-770/session-001-121315-932.bin`,
  **52 CRC-valid frames (2,912 bytes)**, no bad frames, gaps,
  duplicates or unmatched bytes. Strict `--require-stage21c2`
  verdict: **STAGE21-C2 POST-MMU EL1t VIRTUAL-VECTOR GATE PASS**.
  Confirms XNU executed the MMU-enable MSR + ISB, trapped at
  `0x8142f490`, entered the virtual EL1t sync vector
  `0xfffffff007429000`, issued HVC from virtual VA + 4, returned to
  EL2 and restored the original instruction/vector with status zero.
- After the device appeared in fastboot, used the user-confirmed
  `fastboot -s <redacted-device-serial> oem reboot-recovery` (OKAY).
  `adb devices -l` then independently confirmed
  `<redacted-device-serial> recovery product:omni_cactus model:Redmi_6A`.
- First Stage25 boot (SHA256 `4e349952...`) obtained 3 independent
  captures proving the post-MMU trap, but failed its runtime gate with
  status `0x511` due to a single SCTLR readback difference:
  XNU wrote `0x3454593d`; cactus read `0x34d4593d` (bit 23 set).
  Updated the exact hardware readback check and strict parser, then
  rebuilt and successfully retested. Stage23/Stage24 images preserved.
- Full trace conditions and source changes are in
  `shim/TRACE-STAGE21-C2.md`. Next: Stage21-C3, reach actual
  high-VA XNU continuation and discover the first genuine later
  translation/permission fault using the same EL2 USB path.

### 2026-10-23 Stage21-C2 / Stage25 implemented, built offline

- New Stage25 places a reversible BRK #0xe22 at the original
  `0x8142f490` instruction, directly after XNU enables `SCTLR_EL1.M`
  and executes ISB. It replaces the **post-MMU EL1t vector's physical
  backing** at `0x81429000` with a direct HVC to the known EL2
  physical-stack logger. Expects virtual VBAR `0xfffffff007429000`.
- Build now pins exact MMU opcodes, original BRK instruction and vector;
  the strict `--require-stage21c2` parser checks CPU state, page
  descriptors, HVC origin, restored code and zero-status completion.
  All **nine** Stage20/21 targeted tests passed. Real ELF contains calls
  to `stage19_probe` and `stage20_el1_transition`.
- Image `shim/out/cactus/cactus-trace-stage-25-boot.img`,
  45,504,512 bytes, SHA256
  `4e3499523a415e38fa055434ec31e70ae708c75dad210ef814537462079ed623`.
  Android boot-v2 image check passed. See
  `shim/TRACE-STAGE21-C2.md` for captures and pass criteria. **Hardware
  test not yet performed** at the time of the offline build.

### 2026-10-23 Corrected Stage24 / Stage21-C1 hardware PASS

- Flashed corrected `shim/out/cactus/cactus-trace-stage-24-boot.img` (45,504,512 bytes, SHA256 `0a77184128bee1e9573b83a504d86c749ca1b0c27435d709ee470960f9606df1`) to fastboot `<redacted-device-serial>`, `product: cactus`; send, write, reboot all returned OKAY.
- Windows COM25 watcher connected at 11:35:46 and captured `../usb-captures/stage-cactus-20261023-113448-796/session-001-113546-822.bin`: **2,968 bytes, 53 CRC-valid frames**, zero bad frames, gaps, duplicates, or unmatched bytes. `python shim/decode-cactus-usb.py <capture> --require-stage21c1` returned **STAGE21-C1 PRE-MMU EL1t PAGE-TABLE GATE PASS**. Events include `0x21ca`, `0x21c0`, `0x21c1`, `0x21c2`–`0x21c9`, successful recovery markers `0x20a3`, `0x21c5`, `0x20ff`, `0x19ff`.
- A second COM25 session was detected in the same watcher run, indicating a subsequent boot. The device was observed in fastboot after the captures; `fastboot reboot recovery` returned OKAY and ADB then independently confirmed `<redacted-device-serial> recovery product:omni_cactus`. Pulling `/cactus-trace.bin` from this recovery returned `No such file or directory`, which does not invalidate the complete first-session USB gate.
- Next: plan Stage21-C2 checkpoint after MMU-enable `0x8142f48c` using virtual EL1t vectors and an EL2 logger. Keep the corrected Stage24 and Stage23 baseline images.

### 2026-10-23 Stage24 no-COM25 root cause fixed; rebuilt image needs hardware test

- The revised Stage24 image (`e348d65b...`) was flashed twice to verified
  `<redacted-device-serial>`, `product: cactus`. Both attempts returned to TWRP; the
  second had the Windows COM25 watcher confirmed running for 120 seconds.
  No `VID_1209&PID_0001` enumeration, no USB sessions or CLOG frames.
  Recovery `/cactus-trace.bin` remained `CTRC` stage=0 without `CTOK`.
- Source inspection found the exact regression: `shim/linux_stage_probe.S`
  put the sentinel/`stage19_probe`/`CTOK` sequence inside
  `#elif STAGE >= 3 && STAGE <= 23`, but the nested dispatcher added Stage24.
  Consequently the Stage24 entry fell through to `chainload` with *no*
  diagnostic call; this explains the intact trace header and no COM25.
  The USB CDC implementation itself had not been changed from Stage23.
- Changed the shared dispatcher to `#elif STAGE >= 3` (the top-level
  STAGE validity check still caps supported stages at 24), and added a
  fail-closed assembler relocation check in `build_trace_probe.sh` for
  stages 19+ so an accidentally omitted `stage19_probe` call fails builds.
  Confirmed the linked ELF has both `stage19_probe` and
  `stage20_el1_transition`, and `objdump` shows real `bl` calls.
- Corrected Stage24 image rebuilt locally with WSL Ubuntu. Final artifact:
  `shim/out/cactus/cactus-trace-stage-24-boot.img`, 45,504,512 bytes,
  SHA256 `0a77184128bee1e9573b83a504d86c749ca1b0c27435d709ee470960f9606df1`.
  Android boot-v2 image check and seven Stage20/21 targeted tests pass.
  **This corrected image has NOT been flashed or hardware verified.**
  Start the confirmed-working USB watcher before boot; verify serial and
  `product: cactus`, flash only when requested, decode captured session
  with `--require-stage21c1`, and verify TWRP recovery. Stage20-D remains
  the last proven hardware passing baseline.

### 2026-10-23 Stage 21-C1 / Stage24 — built offline, hardware pending

- First Stage24 image SHA256 `9a8721e0a89cdb381fabbded0ccebb40b183c5d599b7153d39823155f3fb010f`
  was flashed and booted back to TWRP, but did not enumerate the shim
  CDC COM25 device or produce raw CLOG frames. Windows saw Xiaomi/ADB
  USB IDs only. Read back TWRP's `/cactus-trace.bin`: untouched
  `CTRC` stage=0 with no `CTOK` marker; root cause still unknown.
- Revised Stage24 prepares a 5.5 s USB polling/capture interval just
  before the EL1t entry. Corrected exception-mode assumption: EL1t
  BRK enters EL1h vector, so HVC's `SPSR_EL2.M` is 5, and saved
  `SPSR_EL1.M` independently retains 4. Updated parser/tests/docs.
  Offline rebuild and 7 targeted tests passed. Revised Stage24 image
  SHA256 `e348d65b798e1a45112df5ded867a830d67f099b032a3784802178fe9a0fbbe7`.
  **Not flashed yet**.

- Added Stage24 pre-MMU EL1t instrumentation, CLOG strict decoder,
  pinned payload checker and negative/mutation tests. Detailed capture
  gates and commands: `shim/TRACE-STAGE21-C1.md`.
- Boot v2 image `shim/out/cactus/cactus-trace-stage-24-boot.img`
  **45,498,368 bytes**, SHA256
  `9a8721e0a89cdb381fabbded0ccebb40b183c5d599b7153d39823155f3fb010f`.
  Existing Stage20-D image hash remains
  `15acded79146d78971436bee7ac2fe14c3c6f7dcf3cbd4854798466f616f281c`.
- Target `0x8142f474` (last instruction before virtual VBAR
  installation), reversible BRK #0xe21. After XNU switches to EL1t
  and sets virtual SP_EL0/SP_EL1 while MMU is still off, temporary
  EL1t sync vector `0x8142e000` issues **direct HVC #0x21**.
  EL2 uses its own physical stack to capture EL1 syndrome,
  TCR/TTBR0/TTBR1/MAIR/SCTLR/VBAR and 2MiB physical/high-VA/FB
  descriptors, then restores code and vectors before TWRP.
- Offline image build + 7 targeted tests passed. Hardware trace
  **NOT captured**, fastboot device not verified connected on this
  attempt, so no flash and no claim of Stage21-C1 hardware success.
- Next: run watcher, verify fastboot product=cactus and flash this
  image under existing user authorization; decode COM25 session using
  `--require-stage21c1`. Only after strict hardware pass proceed
  to Stage21-C2 MMU-on image.

### 2026-10-22 Stage 21-B MMU audit — OFFLINE PASS, no flash

- Parsed the exact relocated Stage20-D SWEETXNU payload
  `shim/out/cactus/linux-chainload/stage18-payload.bin`
  (SHA256 `f1cbeff59113e27a66e4ed70e4b2b59c5318aa36825ac0d961040f7ae3bb5b1f`).
  Added guarded read-only checker `shim/check_stage21b_mmu.py`
  and mutation tests `shim/test_stage21b_mmu.py`. Full detailed
  evidence and Stage21-C traps: `shim/TRACE-STAGE21-B.md`.
- XNU **hardware granule is 4 KiB**:
  `TCR_EL1=0x22a5192519` (TG0=0, TG1=2, T0SZ=T1SZ=25,
  IPS=2); 16 KiB XNU VM/software pages are handled separately by
  the cactus pmap patches. 4 KiB table pool
  `0x8117a000..0x8117e000`, TTBR0 `0x8117a000`,
  TTBR1 `0x8117b000`, MAIR `0x0c08044f00bb44ff`.
- First **MMU-enable** `msr sctlr_el1,x0` at physical
  **`0x8142f488`** (`SCTLR=0x3454593d`) then ISB
  `0x8142f48c`. Before it, XNU switches to **EL1t**
  (`msr spsel,#0` at `0x8142f18c`) and installs virtual
  VBAR `0xfffffff007429000` at `0x8142f474`.
  Stage21-C's post-MMU synchronous vector is **EL1t**, backed by
  physical **`0x81429000`**, not Stage20-D's EL1h slot.
- The bootstrap maps core text, ADT, boot_args, and early stack.
  An intended 4 MiB high-VA alias covers the full 720×1440
  visible framebuffer. An apparent **2 MiB mapping gap** at
  `[0x9de00000,0x9e000000)` needs validation from real
  descriptors; do not assume it is the cause of a crash.
- Next: Stage21-C **pre-MMU** table/sysreg trace before PA
  `0x8142f474`, then **post-MMU** checkpoint after
  `0x8142f48c` using mapped EL1t vector/HVC-only EL2
  logger. These are proposed, not yet built/tested. Preserve
  Stage20-D recovery and its x19–x30 save/restore fix.

### 2026-10-22 Stage20-D hardware PASS after EL2 callee-saved-register fix

- Built stage **23** locally in elevated WSL and flashed to verified
  serial `<redacted-device-serial>`, `product: cactus`. Initial build
  SHA256 `dea32705a291e287290e51c34eeefe64305fc0f9c513d0568a9f670c2ed03029`;
  sending, writing and reboot each returned fastboot `OKAY`.
  COM25 trace from
  `../usb-captures/stage-cactus-20261022-212852-301/session-001-212901-784.bin`
  had **41 valid frames**, proving XNU changed VBAR_EL1, took
  `BRK #0xd20` at `0x8142f154`, logged VBAR `0x8142e000`,
  x20 boot_args and all three boot-args values, sampled both
  framebuffer pixels white, and executed HVC #0x21.
- The initial build aborted `0x19ee` after the HVC: EL2
  `ESR=0x96000000`, `ELR=0x41ec5c14`,
  `FAR=0xfffffff006000758`. Disassembly pinpointed
  `stage19_probe+0x454` loading a pointer via `x22`. XNU had
  legitimately overwritten x22 with its virtBase. The EL2-to-EL1
  trampoline saved only x29/x30 and violated the AAPCS64 contract.
- Fixed `shim/linux_stage_probe.S` to save/restore **x19–x30**
  as six 16-byte register pairs around the XNU handoff; verified the
  ARM64 machine code before rerunning. Rebuilt and reflashed the
  corrected boot v2 image, **45,504,512 bytes**, SHA256
  `15acded79146d78971436bee7ac2fe14c3c6f7dcf3cbd4854798466f616f281c`.
  Again sending, writing and reboot returned `OKAY`.
- The first second-run background watcher missed COM25 due host-side
  capture/permission issues; **ADB confirmed TWRP recovery** and
  pulled `../stage20d-fixed-twrp-trace.bin` with `CTOK stage=23`.
  Replayed the already-installed image from TWRP through verified
  fastboot with the watcher running **elevated in foreground**.
  Capture `../usb-captures/stage-cactus-20261022-214238-192/session-001-214300-964.bin`
  contains **47 CRC-valid Stage23 frames / 2,632 bytes**; zero
  gaps, duplicates, corruption or unmatched bytes.
  `--require-stage20d` returned **STAGE20-D POST-VBAR EL1h VECTOR PASS**:
  `0x20d6` vector patched, `0x20d1` ESR `0xf2000d20`,
  `0x20d2` VBAR `0x8142e000`, `0x20d3` boot args,
  `0x20c2` both framebuffer pixels `0xffffffff`,
  `0x20b3` HVC ESR `0x5a000021`,
  `0x20a3` returned EL2, `0x20d5 (1,1,1)` restored
  vector and BRK, and `0x20ff`/`0x19ff` statuses zero.
  **ADB independently verified TWRP recovery**:
  `<redacted-device-serial> recovery product:omni_cactus model:Redmi_6A`.
  This proves genuine XNU execution through VBAR_EL1 and synchronous
  exception trapping, before MMU enable. It does not prove full XNU boot.
  See `shim/TRACE-STAGE20-D.md`.

### 2026-10-22 Stage20-D post-VBAR EL1h exception — source implemented; hardware UNTESTED

- Added isolated build stage **23**, preserving Stage20-C image and its hardware PASS.
  Actual XNU startup executes its `msr vbar_el1,x0` at PA
  `0x8142f144`, then reads `boot_args` values into x22/x23/x24.
  Temporarily patch XNU's EL1h synchronous vector at `0x8142e200`
  to branch through an absolute runtime-resolved shim handler, and put
  `BRK #0xd20` at `0x8142f154`. Handler emits ESR/ELR/FAR,
  actual VBAR_EL1, x20 boot_args, x22/x23/x24 boot memory
  values and sampled framebuffer pixels, HVCs back to EL2, restores
  both modified instruction and XNU vector, then attempts TWRP.
- New pinned-payload admission gate `shim/check_stage20d_entry.py`
  passed against the actual relocated payload; it checks exact VBAR,
  four original vector words and four post-VBAR instructions.
  `test_stage20d_entry.py`, `test_stage20d_usb.py`, and
  `test_stage20c_usb.py` each passed on Windows Python.
  Strict `decode-cactus-usb.py --require-stage20d` checks 10
  segment copies, exact vector and trap records, boot args, HVC,
  restoration, completion, and continuous CRC-valid frames.
- **No Stage20-D image built or flashed yet.** WSL invocation
  returned `WSL/E_ACCESSDENIED`, the escalated WSL build attempt
  was blocked by a safety check. A separate combined unittest call
  was also blocked; individual Python tests above succeeded.
  Until WSL cross-compilation and image v2 validation succeed,
  treat stage 23 as an offline source candidate only.
  See `shim/TRACE-STAGE20-D.md` for exact resume commands and
  hardware checkpoints.

### 2026-10-22 Stage20-C early XNU framebuffer / pre-VBAR probe

- **HARDWARE PASS, 18:25 local:** verified `fastboot` serial
  `<redacted-device-serial>`, `product: cactus`, and flashed verified stage-22
  image, SHA256 `a78876e8f95089c3a13f2c08a3c29b1e7ea12ab49caaf03db5fb625d5a607dc8`.
  Send, write, reboot all `OKAY`. Raw USB capture
  `../usb-captures/stage-cactus-20261022-182508-915/session-001-182538-398.bin`
  decoded **44 CRC-valid stage22 frames / 2464 bytes**, no noise,
  corruption, duplicates or sequence gaps. Strict
  `--require-stage20c` returned **EARLY XNU FRAMEBUFFER/VBAR GATE PASS**.
  `0x20c1` EL1 ESR `0xf2000c20`, ELR `0x8142f144`;
  `0x20c2` reported XNU x20/boot_args `0x824bc000` and
  both framebuffer white pixels `0xffffffff`. HVC `0x20b3` ESR
  `0x5a000021`; `0x20c3 (1,1,1)`; both DONE statuses zero.
  **ADB confirmed successful TWRP recovery**:
  `<redacted-device-serial> recovery product:omni_cactus model:Redmi_6A`.
  This establishes XNU's real early framebuffer loop executed until
  its pre-VBAR breakpoint; no XNU vector switch or MMU bring-up yet.

- Built separate stage **22**, reusing proven Stage20-B XNU loading,
  EL1 branch, COM25 CLOG tracer and return to TWRP.
- Offline code admission pins 23 exact AArch64 instructions from
  `0x8142f0ec` through `0x8142f144` (SHA256
  `baa9674ab5b60ca787be5b7c7d8f8e54b552f0f85f4f09e50535c85395529ebc`).
  XNU would draw a 64×32 white marker at the bottom-left of the
  720×1440 framebuffer; the temporary BRK #0xc20 stops immediately
  before `msr vbar_el1,x0` replaces the shim's vectors.
- New event `0x20c1` captures ESR/ELR/FAR at the trap;
  `0x20c2` reports XNU's saved boot_args x20 and first/last marker
  pixels. `0x20c3` confirms original VBAR instruction restoration,
  trap and HVC return. The strict `--require-stage20c` USB decoder
  verifies all copy and EL1 checkpoints, marker samples and TWRP path.
- WSL offline build: `shim/out/cactus/cactus-trace-stage-22-boot.img`
  **45,504,512 bytes**, SHA256
  **a78876e8f95089c3a13f2c08a3c29b1e7ea12ab49caaf03db5fb625d5a607dc8**.
  Boot image v2 check passed; payload mutation test, USB decoder
  synthetic-good/bad test, existing nine USB tests, and host trace ring
  tests passed. The proven Stage20-B image hash was unchanged.
- Whether
  the white rectangle is visible depends on whether the display and
  backlight are active; USB/memory markers are authoritative.
  Details: `shim/TRACE-STAGE20-C.md`.

### 2026-10-22 Stage20-B first real XNU instruction offline candidate

**HARDWARE PASS (2026-10-22):** Verified fastboot serial
`<redacted-device-serial>`, product `cactus`, then flashed
`shim/out/cactus/cactus-trace-stage-21-boot.img` (45,504,512 bytes,
SHA256 `979a54dead1dbaa729f4d0a82269cda538f8dbfd8baa5856527015aba0aedd9c`).
Send, write and reboot each reported fastboot OKAY. COM25 USB capture
`../usb-captures/stage-cactus-20261022-174227-147/session-001-174229-019.bin`
yielded **43 valid frames, 2,408 bytes, zero gaps, corruption or noise**.
Decoder `--require-stage20b`: **STAGE20-B FIRST REAL XNU BRANCH PASS**.
Ten segment copies, ADT and boot_args all passed; EL1 event `0x20a1`
confirmed CurrentEL=4; event `0x20b2` ESR `0xf2000b20` with
ELR `0x8142f0ec` confirmed XNU's first `b` instruction reached the
temporary BRK; EL2 `0x20b3` ESR `0x5a000021` confirmed HVC #0x21;
`0x20b4` args `1/1/1` confirmed original destination restoration,
and `0x20ff` and `0x19ff` reported zero status.
`adb devices -l` verified `<redacted-device-serial> recovery product:omni_cactus
model:Redmi_6A device:cactus`. **TWRP successfully returned.** This
proves one genuine XNU branch instruction, not a running XNU kernel.

- Stage20-A EL2-to-EL1 and return to TWRP were confirmed on hardware.
- Stage20-B is isolated as build stage 21 to preserve that image.
  The actual SWEETXNU entry instruction at PA 0x8142d488 is
  0x14000719 (A64 B +0x1c64) to PA 0x8142f0ec, whose original word
  is 0xd510109f. Offline and runtime checks pin these exact bytes.
- The probe temporarily replaces the branch destination with
  BRK #0xb20, executes the first authentic XNU branch at EL1, logs
  its EL1 exception and EL2 HVC #0x21, restores the destination
  instruction and attempts the known-good TWRP chainload.
- New --require-stage20b decoder requires all segment copies,
  correct BRK ESR/ELR and HVC syndrome, EL2 return, restored word,
  zero-status completion and a valid uninterrupted USB trace.
- This tests at most the **first genuine XNU instruction**; the original
  branch-target instruction is not executed. Hardware proof remains
  pending. Details: shim/TRACE-STAGE20-B.md.
- WSL offline build completed: cactus-trace-stage-21-boot.img,
  **45,504,512 bytes**, SHA256
  **979a54dead1dbaa729f4d0a82269cda538f8dbfd8baa5856527015aba0aedd9c**.
  Android boot v2 check passed; payload positive/mutation test (1),
  USB decoder tests (9) and native CLOG ring test (1) all passed.
  AArch64 disassembly verifies branch to real XNU entry, HVC #0x21,
  and explicit instruction cache maintenance. **No hardware flash**.

### 2026-10-22 Stage20-A EL2 -> EL1 -> EL2 hardware PASS; TWRP recovery confirmed

- Windows fastboot verified serial `<redacted-device-serial>`, `product: cactus`;
  flashed `shim/out/cactus/cactus-trace-stage-20-boot.img` to boot
  and rebooted. Both fastboot operations returned `OKAY`.
  Image 45,502,464 bytes, SHA256
  `b078871f8e8fd80e5772744a24835ab634565e3746614959094c6838a1bab37c`.
- USB COM25 watcher captured
  `../usb-captures/stage-cactus-20261022-170729-186/session-001-170820-150.bin`:
  **40 CRC-valid CLOG frames / 2,240 bytes**, zero corrupt frames,
  sequence gaps, duplicates or regressions. The strict decoder
  `--require-stage20` returned **`STAGE20 EL1 ERET/HVC RETURN PASS`**.
- All 10 actual XNU segment start/complete pairs were present, followed by
  ADT, boot_args and copy-completion markers. `0x20a0` logged pre-ERET
  at EL2. **`0x20a1` was emitted at EL1** (CLOG `EL=4`, `a=4`),
  proving the shim-owned EL1 trampoline executed. The EL2 HVC trap
  marker `0x20a2` contained ESR `0x5a000020` (EC=0x16, HVC #0x20)
  and saved SPSR `0x600003c5`. `0x20a3` showed successful EL2 return
  (`EL=8`, `a=8`). Final `0x20ff a=0` and `0x19ff a=0` confirmed
  normal diagnostic completion. No `0x19ee` fault occurred.
- **TWRP chainload confirmed by Windows ADB**:
  `<redacted-device-serial> recovery product:omni_cactus model:Redmi_6A`.
  This establishes CPU handoff and recovery return independently of XNU.
  Stage20-A **never jumped into XNU**; Stage20-B should probe the
  *first actual XNU instruction* and capture any early CPU exception.

### 2026-10-22 GitHub checkpoint and Stage20-A local build

- Published all current cactus bring-up sources, USB diagnostics,
  Stage14-19 notes and regression tests to `origin/cactus-phase1`
  commit **`923f901`** (`fd52053..923f901`). GitHub push succeeded.
  Tests before push: seven USB decoder tests, host trace ring,
  Stage15 FDT tests, Stage18 actual-payload/17-reservation layout test
  and `git diff --cached --check`. Windows-native ring test could not
  spawn `cc`; the same test passed under WSL Ubuntu.
- Stage20-A added to `shim/build_trace_probe.sh`,
  `shim/linux_stage_probe.S`, `shim/stage19_probe.c` and CLOG decoder.
  Reuses successful Stage19 segment+ADT+boot_args installation,
  then ERET to shim-owned EL1 CLOG checkpoint, HVC #0x20 back to EL2
  and existing TWRP recovery chainload. It does **not** enter XNU.
- WSL crossbuild generated
  `shim/out/cactus/cactus-trace-stage-20-boot.img`, size
  **45,502,464 bytes**, SHA256
  `b078871f8e8fd80e5772744a24835ab634565e3746614959094c6838a1bab37c`.
  Boot header v2 validator and disassembly checks passed. The first
  build encountered an implicit `memcpy` reference in freestanding
  code; this was resolved before successful linking. The Stage20
  CLOG ring is 64 slots, with a strict `--require-stage20` decoder.
- **This was the pre-hardware build status.** Stage20-A has since been
  flashed and passed the EL1/HVC return test with TWRP confirmation,
  as recorded in the newer hardware section above. See
  `shim/TRACE-STAGE20-A.md` for the capture and exact checkpoints.

### 2026-10-22 Stage19-A real segment-copy hardware PASS and recovery confirmed

- Stage19-A boot image was flashed to verified cactus boot partition.
  `shim/out/cactus/cactus-trace-stage-19-boot.img` 45,502,464 bytes,
  SHA256 `804c5fbdf986fe24b87032587d70d9aa3e6ece6e311f70a25065203681679f1d`.
  Windows fastboot send/write/reboot each reported OKAY.
- COM25 hardware trace from `../usb-captures/stage-cactus-20261022-163123-662/session-001-163156-116.bin`
  recorded 35 events / 1,960 bytes. All 10 segment start/done pairs
  (`0x1940`/`0x1941`) emitted, followed by ADT copied (`0x1950`),
  boot_args written (`0x1951`), real payload copy complete
  (`0x1952`) and DONE (`0x19ff a=0`).
- Physically loaded real XNU bytes into checked
  `[0x80000000,0x9e000000)`, copied Apple ADT to `0x824b8000`,
  wrote boot_args to `0x824bc000`, calculated XNU entry
  `0x8142d488`. The Stage19 implementation samples copied content
  (three 32-byte windows per segment, BSS head/tail), not a full
  bytewise comparison of every loaded byte.
- The user subsequently confirmed **TWRP booted successfully**.
  An attempted ADB confirmation had previously failed because the
  host ADB server could not start; the physical recovery observation
  supersedes that unresolved host-side check.
- No XNU instruction or EL2->EL1 transfer occurred in Stage19-A.
  Stage20-A target: shim-owned EL1 ERET checkpoint with USB confirmation,
  return to EL2 and established TWRP recovery chainload before Stage20-B
  attempts a genuine XNU instruction.

### 2026-10-22 Stage18 aligned hardware PASS and TWRP recovery confirmed

- Windows fastboot was visible when invoked with escalated USB access;
  verified serial `<redacted-device-serial>`, `product: cactus`, and corrected image
  SHA256 `ead3617f6738b7a019da421826a8ad6f85c16a1297404645ed2659c8445cb152`.
  Flashed `shim/out/cactus/cactus-trace-stage-18-aligned-boot.img` to
  **boot** and rebooted; both fastboot commands returned `OKAY`.
- COM25 watcher captured
  `../usb-captures/stage-cactus-20261022-161840-247/session-001-161903-759.bin`:
  **25 valid Stage18 frames / 1,400 bytes; no errors or gaps**.
  `0x1821 a=1` proves the proposed 480 MiB XNU destination is free in
  live FDT; **`0x1831 a=0` validates the actual relocated payload**, ten
  `0x1840` records show segment destinations, `0x18f0` reports entry PA
  `0x8142d488`, boot_args `0x824bc000`, top `0x824c0000`, and
  **`0x18ff a=0` reports success**. `--require-stage18` PASSED.
- After COM25 disconnected at the 5.5s bounded handoff, ADB showed
  **`<redacted-device-serial> recovery product:omni_cactus model:Redmi_6A`**.
  The TWRP recovery wrapper now demonstrably chainloads successfully.
  This image validates layout but **does not copy or execute XNU**;
  an actual XNU boot and the original standalone-shim black-screen
  cause remain unproven. Next milestone is separately instrumented
  real segment copy and first XNU-owned instruction/EL2→EL1 handoff.

### 2026-10-22 Stage18 hardware alignment abort resolved offline

- Stage18 boot flashed with verified `product: cactus`, serial
  `<redacted-device-serial>`. User's `stage18latestdump.txt` and assistant's
  80s watcher yielded five matching Stage18 CDC sessions, each
  **560 bytes / 10 CRC-valid records**, zero gaps.
- Live FDT parser succeeded (1 RAM bank, 17 reserved intervals).
  Relocated payload `[0x80000000,0x9e000000)` passed read-only FDT
  free check (`0x1821 a=1`). Event `0x1830` showed payload pointer
  `0x52bcbfa4`; next event `0x18ee` reported
  **ESR=0x96000021, ELR=0x41ec4174, FAR=0x52bcbfdc**.
- Disassembly at ELF offset `0x1e44174` shows
  `ldr x9,[x0,#56]` reading 64-bit `phys_base`. Source
  `0x52bcbfa4` was misaligned by 4 bytes; FAR equals
  `source + 0x38`. **Deterministic alignment fault** in
  diagnostic payload reader. No `0x1831` validation, no `0x18f0`
  READY, no `0x18ff` DONE, no TWRP handoff and no XNU execution.
- Repacked CPIO payload with filename padding ensuring 16-byte
  alignment, plus build and runtime checks, and shortened post-verdict
  USB polling 11.5s→5.5s to attempt recovery chainload sooner.
  `shim/out/cactus/cactus-trace-stage-18-aligned-boot.img` built
  **45,502,464 bytes**, SHA256
  `ead3617f6738b7a019da421826a8ad6f85c16a1297404645ed2659c8445cb152`.
  Embedded raw payload address `0x52bcbfb0`, verified 16-byte
  alignment and byte-for-byte content. Bootv2 validator and seven
  decoder regressions pass; host admission still accepts relocated
  payload (status 0) and rejects original (status 4).
- **Corrected image not yet hardware flashed/tested.** `fastboot
  devices -l` saw no connected fastboot device when checked.
  See `shim/TRACE-STAGE18.md` for exact capture/flash steps.

### 2026-10-22 Stage18 REAL XNU payload layout admission — OFFLINE BUILT

- Finished Stage18 in `shim/linux_stage_probe.S` and
  `shim/stage18_probe.c` using the Stage17 hardware-positive USB/TWRP
  wrapper. CLOG stage=18 checkpoints report USB, live FDT, payload
  placement, shared admission verdict, all 10 segment destinations,
  entryPA/boot_args PA and early DONE. No segment copy or XNU execution.
- Stage17 proved physical RAM `[0x40000000,0xc0000000)` with 17
  reservations. Original XNU layout `[0x70000000,0xc0000000)`
  collides with reserved RAM; `shim/prepare_stage18_payload.py`
  creates a *separate* copy relocated to `[0x80000000,0x9e000000)`
  (480 MiB), updating payload header and ADT. Original unchanged.
- Added fail-closed `shim/payload_layout.[ch]`, called by both Stage18
  and **all normal BOARD=cactus loader builds**, independent of
  STAGE15=1, before segment copy. Validates live FDT RAM/reservations,
  active shim/initrd/DTB overlap, segment intervals, entry and
  boot_args placement.
- `./build_trace_probe.sh 18` local Ubuntu build PASS. Image:
  `shim/out/cactus/cactus-trace-stage-18-boot.img`, size
  **45,502,464 bytes**, SHA256
  `6231e0cc6a56414f1664c003f0752e66929f9b1cc7b1bd29bc65e7cc98f3b8d2`.
  Boot v2 validation passed; ramdisk contains 16,540,608-byte copied
  payload. WSL linked symbols present. Normal `BOARD=cactus` loader
  compiled under `TAG=stage18-loader-check`.
- Seven decoder tests passed. Host C test with all 17 Stage17 reservations
  rejected original layout (status 4), admitted copied candidate
  (status 0), entry `0x8142d488`, boot_args `0x824bc000`,
  and rejected malformed segment and reserved-window mutations.
- See `shim/TRACE-STAGE18.md` for USB event map, reconnecting
  capture and hardware test commands. **No Stage18 flash or hardware
  test yet.** A final `fastboot devices -l` on 2026-10-22 returned
  no connected device; no flash was attempted. Next step is a single
  verified fastboot test, then use
  observed `0x1831` and `0x18f0` to decide the bounded real
  copy / EL1 / first-XNU-instruction milestone. The 480 MiB XNU VM
  bootability remains unproven.

### 2026-10-22 Stage17 HARDWARE POSITIVE — live 2 GiB RAM map obtained

- Fastboot verified serial `<redacted-device-serial>`, product `cactus` and
  `shim/out/cactus/cactus-trace-stage-17-boot.img` SHA256
  `bec9d3848b29a80edbd51727d64c40d5767cb0b068f2f609cceaa2d4af9a93d1`,
  28,958,720 bytes. Flash boot / reboot both returned **OKAY**.
- The automatic watcher recorded **two complete CDC attachment sessions**
  into `../usb-captures/stage-cactus-20261022-141803-823/`:
  `session-001-141827-940.bin` and `session-002-141854-774.bin`.
  Each 2,016 bytes / **36 CRC-valid CLOG frames**, zero corrupt records,
  zero gaps/duplicates. USB COM25 detached and reappeared ~27 seconds later.
- **Actual LK live FDT** at `0x47880000`, FDT size `0x28074` (163,956).
  `0x1712` shows parse PASS, **one RAM bank, 17 reserved ranges**.
  `0x1713/0x1714` prove accessing the bank succeeded:
  **physical DRAM `[0x40000000, 0xc0000000)` = 2 GiB**.
- Native reservations include `[0x70000000,0x73c40000)` (at least),
  `[0xae000000,0xbe000000)`, and many other entries. LK initrd is
  `[0x51b00000,0x52bcbf9c)`. **XNU proposed window
  `[0x70000000,0xc0000000)` rejected** by `fdt_region_free` (`0x1717 a=0`).
  A valid loader destination must be selected from actual unreserved DRAM,
  also excluding the running shim/image/ramdisk/FDT, not just editing a
  single fixed address. Do not copy full XNU payload yet.
- `0x1718` map-DONE exists, proving the previous Stage16 absence of
  `0x1613` was avoidable by removing the pre-map delays; parsing the live
  memory table itself succeeds. Nine valid `0x1702` heartbeats continued
  after the full map, DTR eventually asserted. **Final `0x17ff` DONE absent**
  in both sessions; USB detached before completion. No ESR exception
  record observed. This supports investigating consistent USB/reset timing,
  but does not prove which caused the loss of connection.
- Next: use the now proven live memory map to calculate an actually free
  destination for the shim/XNU components and move to a bounded
  EL2-to-EL1 handoff while preserving the recovery-wrapped logger.
  Keep tracing the missing end marker separately. Stage17 did **not**
  execute XNU, alter LK, or enable MMU.

### 2026-10-22 Stage17 live-memory-map-first probe — OFFLINE BUILT

- Read all five sessions saved by the repaired
  `../usb-captures/stage-cactus-20261022-135847-596` 120s receiver.
  Each is exactly **1,176 bytes / 21 CRC-valid Stage16 frames**, with
  the same events through `0x1612` successful live FDT parse and
  heartbeat `0x1602` seq20. Windows re-enumerated COM25 after each
  disappearance (~27s between attachment start times). This is a
  repeatable **device-side or USB transport** cutoff after parsing,
  not a one-time host command race. Exact root cause still unproven.
- Built Stage17 in the **same recovery/TWRP wrapper**:
  `shim/stage17_probe.c` and `STAGE=17` support in
  `shim/linux_stage_probe.S` / `shim/build_trace_probe.sh`.
  Stage17 validates/parses live FDT **immediately** after USB init,
  emits before-bank `0x1713`, bank values `0x1714`, reserved ranges,
  XNU-window verdict and map DONE **before any 7s / 650ms delay**.
  A bounded 15s USB polling window emits heartbeat/DONE, then TWRP
  chainload. `struct fdt_info` lives on the stack; no XNU execution,
  debug RAM setup, or LK modification.
- WSL ARM64 build/boot v2 validation PASS; linked-symbol inspection,
  FDT parser host check and 6 CLOG decoder tests PASS. Candidate
  `shim/out/cactus/cactus-trace-stage-17-boot.img` is **28,958,720
  bytes** SHA256
  `bec9d3848b29a80edbd51727d64c40d5767cb0b068f2f609cceaa2d4af9a93d1`.
  **Not flashed/hardware tested.** `fastboot devices` returned no
  connected device during this review. See
  `shim/TRACE-STAGE17.md` for receiver-first test procedure.

### 2026-10-22 USB capture reconnect reliability

**Hardware and watcher fix, 13:51–13:56 local time:** User tested
`shim/watch-cactus-usb.ps1` while Stage16 CDC was enumerating. Two
independent sessions were saved, both exactly **1,176 bytes / 21 valid
Stage16 CLOG frames**, **0 CRC failures, 0 missing sequence numbers**:
`../usb-captures/stage-cactus-20261022-135126-243/session-001-135126-927.bin`
(SHA256 `9f69a6a012acd3f7b70f5eb2e19be9b4080a38a9b9fb69443e104b5a9be7370c`),
and `../usb-captures/stage-cactus-20261022-135142-642/session-001-135146-183.bin`
(SHA256 `7d4f39f8789d988450d0bb931dac1760726941591bff42695bf51238e2c07000`).
Both include entry, USB-ready, FDT header, and live FDT parse success:
`0x1612 a=1,b=1,c=17`, then exactly one more heartbeat
`0x1602` seq=20. Both lack RAM-bank `0x1613`, debug milestones, and
DONE. This is repeatable truncation **after** FDT parsing; USB port was
subsequently removed by Windows. Cause of device-side cutoff/reboot is
unconfirmed.

Initial watcher had a real PowerShell function-scope bug:
`Close-Session` assigned `$port=$null` / `$stream=$null` only inside
the function's local scope. On detach, the stale top-level references
caused repeated CLOSE messages and an `ObjectDisposedException` from
flushing an already closed file, terminating capture before the next
COM25 reappearance. Fixed by clearing `$script:port` and
`$script:stream` **before** disposing the old objects, guarding
stream flush, and resetting `$script:sessionBytes`. PowerShell AST
parser PASS; a regression test simulated two sessions with a second
cleanup call for each, verified exactly two CLOSE events, idempotent
cleanup, and 56-byte flushed files. A 4-second watcher smoke test
finished normally. **Hardware reconnect after the fix not yet tested.**

- User's Windows USB device monitor observed VID_1209/PID_0001 COM25
  detach/re-enumeration cycles roughly 17-26 seconds apart. Thus the
  missing Stage16 DONE could be a host capture/reboot timing issue;
  repeated physical resets remain a possibility, not proven.
- Added `shim/watch-cactus-usb.ps1`: launch once, auto-reconnect and
  raise DTR/RTS on every COM25 appearance, stream each attachment to a
  fresh session-###.bin, save timestamped events.log, decode each
  nonempty session on clean exit. `-Seconds 0` keeps running until
  Ctrl+C. Default data folder is `../usb-captures/stage-cactus-<time>/`.
- Windows PowerShell parser PASS and two bounded 4-second smoke checks
  completed normally. On this local execution host, `Get-PnpDevice`
  failed because access to CIM resource was unavailable; script now
  falls back to serial-port enumeration (default COM25). No COM25
  detected in fastboot during smoke tests, so reconnect with active
  stage16 USB still needs hardware validation.
- Start script **before** next test and leave it active across several
  USB re-enumerations:
  `powershell -NoProfile -ExecutionPolicy Bypass -File "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\watch-cactus-usb.ps1" -Seconds 120`
  The capture command does not flash or reboot the device.

### 2026-10-22 Stage16 minimal real shim in USB hybrid — OFFLINE BUILT

**HARDWARE UPDATE (2026-10-22): Stage16 partial USB success.**
Flashed `shim/out/cactus/cactus-trace-stage-16-boot.img` after verifying
fastboot `product: cactus`, serial `<redacted-device-serial>`, and expected SHA256
`315120457a337e322f23b0a24bdf38e9f7b2b1a94d560bf23808133385b63668`.
Flash/reboot returned OKAY. Windows opened COM25 and captured 1,176 bytes
to `../stage16-raw.bin`, SHA256
`d95a52c6293fc357c06911b54b47cfdd512d412349aa10943368bb8392fd50361`.
Decoder: 21 CRC-valid stage=16 frames, zero gaps/duplicates/errors;
`0x1600` entry EL2 with DTB `0x47880000`, `0x1601` USB initialized,
`0x1610` before FDT, `0x1611` FDT header accepted (163,956 bytes:
raw size argument `0x28074`), and `0x1612` FDT parsed successfully
(`memory_count=1`, `reserved_count=17`). The latest frame was heartbeat
`0x1602` seq=20. The USB capture ended before `0x1613` DRAM bank,
`0x1620/0x1621` debug checkpoints or `0x16ff` DONE, so strict
`--require-stage16` FAILS; reason for link cutoff unknown. This is
hardware proof of live FDT parsing only, not XNU execution.
Next test must distinguish USB disconnect/reset from execution stall,
inspect live FDT memory bank(s), and preserve early logging beyond
~9 seconds; do not conclude the reserved memory count equals the
stock DTB (stock has 3 reservations, live FDT has 17).

- Followed Stage15 Hybrid's hardware-positive 37 CLOG records on COM25
  by adding Stage16 to the same recovery-wrapped entry in
  `shim/linux_stage_probe.S` / `shim/build_trace_probe.sh`.
  Preserved recovery chainload and host CLOG transport; selects
  VBAR_EL2 on observed EL2 and captures exception ESR/ELR/FAR.
- New `shim/stage16_probe.c` streams stage=16 checkpoints around the
  actual cactus shim's `fdt_check`, Benesse-derived `fdt_inspect`,
  memory bank and reservation enumeration, XNU RAM-window admission
  check and `dbg_init` reserved-RAM setup. No kernel copy, XNU
  entry, MMU enable, or LK flash. The original shim's other startup
  routines and XNU's first instruction remain future milestones.
- Built with WSL `./build_trace_probe.sh 16`: resulting
  `shim/out/cactus/cactus-trace-stage-16-boot.img` is **28,958,720
  bytes**, SHA256
  `315120457a337e322f23b0a24bdf38e9f7b2b1a94d560bf23808133385b63668`.
  Android boot-v2 validation, ARM64 linked symbol checks, and five
  CLOG decoder tests passed. **No hardware flash/test for Stage16 yet.**
- Test instructions and strict decoder gate `--require-stage16`
  documented in `shim/TRACE-STAGE16.md`. Give the probe at least
  30 seconds before manual fastboot reset. Next evidence is the
  live RAM bank map and XNU window admission verdict.

### 2026-10-22 Stage15 Hybrid HARDWARE USB POSITIVE (partial preflight)

- Verified fastboot serial `<redacted-device-serial>`, product `cactus`; flashed
  `shim/out/cactus/cactus-trace-stage-15-boot.img` (28,956,672 bytes,
  SHA256 `07bde34f600187c9208f9c83b601af835217576838cfd9504b543423bd322334`).
  Flash boot and fastboot reboot both returned OKAY.
- Windows receiver opened **COM25** with DTR/RTS, saved 2,072 bytes to
  `../stage15-hybrid-raw.bin` (SHA256
  `d697f9f9b212e1f1001897546af0e37ee8c81ec6de8b75e4627e7df39fa078a8`).
  Decoder: **37 valid Stage15 CLOG frames, zero bad CRC, zero sequence gaps,
  zero duplicates/regressions**; session `0xca15e001`, EL2=0x8.
- Verified events: `0x1580` entry, `0x1581` USB initialized successfully,
  and 35 changing `0x1582` heartbeats. Entry DTB=0x47880000,
  MPIDR=0x80000000; USB HWVERS=0xe503, POWER=0x20.
  Heartbeat timestamps span ~9 seconds and DTR becomes 0x3.
- Expected `0x158a` DONE was **not captured**, so
  `--require-stage15-early --early-level 1` reports NOT VERIFIED.
  This is **positive hardware proof of Stage15 C logger + proven Stage14
  recovery-wrapped entry**; the *complete timed probe / recovery handoff*
  is still not verified. No XNU instructions were executed.
- Both stand-alone Stage15 real-shim entry images delivered 0 bytes.
  Next: preserve the now-proven recovery-wrapped entry and introduce
  real shim startup components one-at-a-time, with CRC milestones;
  investigate missing DONE (USB stream ended before the 17-second
  diagnostic, possible reset, stalled poll, or link loss, cause unknown).
  Do not change LK or flash full XNU yet.

### 2026-10-22 Entry-wrapper comparison and Stage15 hybrid (offline)

- Compared actual gzip-decompressed boot kernel Images: the
  **hardware-positive Stage14** wrapper is 31,728,535 bytes and
  executes its USB probe near the end of an embedded recovery Image;
  **both hardware-failed Stage15 Early** variants use a 172,032-byte
  real shim Image whose `_start` is at offset 0x800. Both Image
  headers report the correct `image_size`, `ARMd` magic and
  0xa flags. Failure cause remains unproven.
- Built new `STAGE=15` mode in `shim/linux_stage_probe.S` and
  `shim/build_trace_probe.sh`: keeps Stage14's verified entry,
  recovery wrapper, sentinel and TWRP chainloader but calls the
  Stage15 early C USB logger. This is the focused next differential
  test, with no XNU execution and no LK modification.
- New image `shim/out/cactus/cactus-trace-stage-15-boot.img`,
  28,956,672 bytes, SHA256
  `07bde34f600187c9208f9c83b601af835217576838cfd9504b543423bd322334`.
  WSL cross-build, Android boot v2 validator, ELF symbol checks and
  four USB decoder tests PASS. **Not flashed or hardware-validated.**
- See `shim/TRACE-STAGE15-HYBRID.md` for the exact first test.

### 2026-10-22 Stage15 Early USB SMALL image: hardware failure

- User restored fastboot, noted the fastboot display itself remains off
  although USB commands work; the black panel is therefore not reliable
  evidence of execution or a CPU hang.
- Verified fastboot serial `<redacted-device-serial>` / product `cactus` and
  `shim/out/cactus/shim-stage15-early-usb-boot.img` length 245760,
  SHA256 `f3bf69ff42e5e5437a9e858f9cc83b785ebfea906e079226a51d8c5b7784ce9c`.
  Flash boot and reboot returned OKAY.
- Host capture listening for `VID_1209&PID_0001` started before reboot.
  It detected no CDC port and saved **zero bytes** to
  `../stage15-early-small-raw.bin`. Decoder reported
  `STAGE15 EARLY NOT VERIFIED`, all early markers absent. No fastboot
  device was reported after this boot.
- This small image uses no XNU ramdisk and bypasses `shim_main`, debug,
  watchdog, framebuffer, FDT and XNU. The earlier matching-payload
  image also produced no USB. Thus image size alone is unlikely to be
  the cause. **Next task**: binary-compare the proven Stage14 Linux
  stage probe entry/packaging against actual shim `_head/_start` and
  build a controlled hybrid with the Stage14 proven entry and new
  framed Stage15 probe, before any additional device flash.

### 2026-10-22 Stage 15 Early USB payload-matched HARDWARE FAIL

- Verified fastboot `<redacted-device-serial>`, product `cactus` and image SHA256
  `613fffd85f6082b35acf3398eafcdc721858135b9c24ad2700f373a4264a9d0c`.
  `fastboot flash boot shim-stage15-early-payload-boot.img` and reboot
  both returned OKAY.
- Windows CDC receiver ran before reboot but did **not** enumerate COM
  and saved **0 bytes** to `../stage15-early-payload-raw.bin`.
  Decoder: `STAGE15 EARLY NOT VERIFIED`, all markers missing.
- ADB device check was inconclusive because `adb.exe` could not start
  its daemon (`could not read ok from ADB Server`). No fastboot device
  appeared in the post-test output.
- This build skips normal `shim_main` debug, WDT, framebuffer, FDT and
  XNU operations. Failure now points earlier: actual shim Linux-image
  startup, entry assumptions/stack/exception vectors, or image layout.
  It does not isolate a cause without more evidence. Next A/B candidate
  is `shim-stage15-early-usb-boot.img` (245760 bytes, no payload),
  then compare working Stage14 image entry/ramdisk format if necessary.

### 2026-10-22 Stage 15 EARLY USB isolation built (offline)

- Stage 15 preflight had no CDC/COM and 0 raw bytes although its flash
  and reboot succeeded; importantly it did **not** execute XNU.
  `shim_main` performed `dbg_init`, `wdt_init` and `mark` before
  initializing USB. Exact failing operation was not observed.
- Added `STAGE15_EARLY=1..4` in `shim/build.sh`, an early direct
  branch in `shim/start.S` after vectors/stack/BSS, and
  `shim/stage15_early.[ch]`. Level 1 uses the Stage 14 hardware-positive
  USB code alone; level 2 adds debug RAM, level 3 MTK watchdog, and
  level 4 live FDT parsing, each bracketed by distinctive CRC events.
  No XNU copy/branch, framebuffer or LK modification.
- Verified AArch64 disassembly: `_start` directly branches to
  `stage15_early_main` and skips `shim_main`. All probe images
  locally cross-built and passed Android boot v2 layout validation.
- **First candidate** `shim/out/cactus/shim-stage15-early-payload-boot.img`
  SHA256 `613fffd85f6082b35acf3398eafcdc721858135b9c24ad2700f373a4264a9d0c`
  (16,785,408 bytes), same SWEETXNU ramdisk size as failed preflight
  but deliberately never executes the payload. Small/no-payload
  level-1 image SHA256
  `f3bf69ff42e5e5437a9e858f9cc83b785ebfea906e079226a51d8c5b7784ce9c`
  (245,760 bytes) is the A/B fallback for a missing USB interface.
- Offline regression tests passed: actual C CLOG ring/CRC, USB parser,
  bounded parser against original cactus DTB; Windows capture PowerShell
  syntax passed. **No Stage 15 Early image flashed or hardware-tested.**
- Test instructions, event IDs and matched level-2/3/4 hashes:
  `shim/TRACE-STAGE15-EARLY.md`. Next test must confirm CDC COM and
  receive Stage 15 Early entry/heartbeat/end events before attempting
  the original Stage 15 preflight or full XNU image.

### 2026-10-22 Stage 15 preflight hardware test: no USB enumeration

- Verified fastboot serial `<redacted-device-serial>`, product `cactus`, image
  `shim/out/cactus/shim-stage15-preflight-boot.img` SHA256
  `e4af2303aa57fd5143952db6188a4192fdf40125fc4433a6403b8b0507e70589`
  (16,783,360 bytes). Fastboot flash and reboot both returned OKAY.
- Windows capture started before reboot and saved
  `../stage15-preflight-raw.bin`: **0 bytes**. No temporary CDC COM
  port reported by the receiver; the decoder reported
  `STAGE15 NOT VERIFIED` with all required checkpoints missing.
- Post-test read-only ADB/fastboot checks returned no device. No
  TWRP trace or XNU execution evidence was recovered.
- Do not flash the Stage15 full-XNU image yet. Isolate whether real
  shim startup reaches `stage15_begin` and whether earlier console/
  watchdog initialization hangs; compare the Stage14 working probe
  entry/USB setup. The cause remains unconfirmed without a trace.

### 2026-10-22 Stage 15 real shim USB preflight — built offline

- Implemented VBAR_EL2 selection at cactus LK entry, VBAR_EL1 before
  EL2-to-EL1 ERET, and current-EL exception registers in `shim/start.S`
  and `shim/shim.c`.
- Extracted Benesse-inspired bounded memory/reserved-memory parsing to
  `shim/fdt_ranges.[ch]`; enforced the live FDT DRAM window, no
  overlap with FDT/initrd/shim, pairwise disjoint segments and a
  file-backed kernel entry before XNU memory copy in `shim/loader.c`.
- `shim/stage15.[ch]` integrates the proven Stage14 polled CDC and CLOG
  event ring into the real shim: entry, USB, FDT, payload, memory
  layout, segments, boot_args, EL1 ready, branch, error and fault.
  Diagnostic-only build stops after validation without copying XNU.
- Host tests PASS: `test_stage15_fdt.py` against original cactus DTB,
  plus Stage14 trace-ring and USB decoder regression tests. The original
  DTB advertises one 512 MiB bank at `0x40000000`; current XNU payload
  requests 1.25 GiB at `0x70000000`. The live LK FDT could differ.
- Local WSL AArch64 builds and Android v2 image validation PASS:
  `shim/out/cactus/shim-stage15-preflight-boot.img` SHA256
  `e4af2303aa57fd5143952db6188a4192fdf40125fc4433a6403b8b0507e70589`,
  and `shim/out/cactus/shim-stage15-xnu-boot.img` SHA256
  `10c8e7ce46a5ec02895a821bc3b1e050693f44cff70d3c8dedec5bab468c7697`.
- **No Stage15 flash/hardware evidence yet.** Test the preflight image
  first with the Windows capture active; success requires stage 15
  ordered host checkpoints and an accepted live RAM map. Stage 16 still
  needs the first XNU-owned marker. Full details in
  `shim/TRACE-STAGE15.md`.

### 2026-10-22 Stage 14 raw binary CDC transport HARDWARE PASS

- User confirmed fastboot. Native fastboot verified serial
  `<redacted-device-serial>`, product `cactus`, and exact corrected Stage 14
  image SHA256 `24edb70a28724f93e46d43e8f53e037f73c96f81c228ceaa1976a16d562045e4`
  (28,956,672 bytes). Fastboot flash and reboot both returned OKAY.
- Windows opened CDC COM25 with DTR/RTS; deliberate close/reopen
  succeeded. Saved `../cactus-stage14-corrected-raw.bin`: 5,544 bytes,
  SHA256 `74b564feb44d246510696df54fa8947712db9e11dcd54c4684c5d2e1ea3c08dd`.
- Python `decode-cactus-usb.py --require-stage14`: **STAGE14 PASS**,
  99 unique valid 56-byte CLOG v2 events, CRC errors **0**,
  unmatched bytes **0**, duplicate **0**, sequence regressions **0**,
  **10 missing sequence numbers**. Session `0xca140001`, event types
  `0x1400`, `0x1401`, `0x1402`, `0x1403`; CurrentEL=EL2. This
  proves arbitrary varying early-stage binary payload, not XNU logs.
- Native ADB/fastboot check after USB session found no device.
  No recovery trace recovered. The host capture remains valid regardless.
- Capture script attempted `SerialPort.Close()` after USB disconnect
  and raised IOException. Patched cleanup to tolerate detach; syntax
  should be rechecked before the next device test. Further work:
  bound delivery loss (10 gaps), confirm shutdown/TWRP handoff, then
  integrate the trace ring at real shim entry with Benesse EL/FDT guards.

### 2026-10-22 Stage 14 reusable CDC / binary event stream

- Added freestanding MUSB `cactus_usb.[ch]` using the Stage 12
  hardware-positive EP0/PHY code, `cactus_trace.[ch]` with a
  32-entry CRC32 ring, `stage14_probe.c` with varied CLOG v2
  records, and Windows streaming capture + Python decoder.
- WSL local cross-build, Android boot v2 image validator, C ring
  CRC/overflow tests, Python decoder tests and PowerShell syntax PASS.
- Two user-authorized hardware flashes/reboots PASS, Windows enumerated
  COM25 both times, but captured **zero bytes**. Root cause found:
  Stage 14 erroneously used EP0 TXPKTRDY bit 1 for EP1/EP2; these
  endpoints require TXCSR **bit 0**. Corrected and rebuilt.
- **Ready-to-test offline image only**:
  `shim/out/cactus/cactus-trace-stage-14-boot.img`,
  SHA256 `24edb70a28724f93e46d43e8f53e037f73c96f81c228ceaa1976a16d562045e4`.
  This corrected image is NOT hardware-verified; fastboot was absent
  after the last test. User will need to restore fastboot.
- See `shim/TRACE-STAGE14.md` for exact commands and acceptance
  criteria. Original verified Stage 12 image is unchanged.

### 2026-10-22 Raw USB -> XNU logging source review / implementation gates

- Reviewed the proven Stage 12 MUSB CDC path, the standalone
  Linux/TWRP trace probe, cactus `shim/start.S` and XNU
  `start.s` patch points, and the Benesse kernel-owned entry
  marker. Detailed, source-grounded design and pass/fail gates:
  `XNU-USB-LOGGING-PLAN.md`.
- **Next action: Stage 14** reusable CDC transport, reserved trace
  ring, varied binary events and streaming Windows decoder. Follow
  with Stage 15 Benesse-style EL/FDT preflight and full shim logs;
  Stage 16 first XNU-owned entry marker; Stage 17 XNU post-MMU
  USB mappings/early console/exception records. Do not confuse
  Stage 12 fixed CLOG proof with streaming XNU runtime logs.
- Source and plan review only: **no new kernel/shim build or device
  flash** in this step. Other working-tree changes are preserved.

### 2026-10-22 Stage 12 raw USB transport HARDWARE PASS

- Root cause of failed EP0 enumeration: after a control-IN response,
  `musb_cdc_probe.c` had discarded a subsequent 8-byte USB SETUP
  as though it were an OUT status packet. The MTK Linux 4.9 driver
  explicitly handles `STATUSOUT -> setup` when RXPKTRDY is set.
  Patched status-out processing to decode COUNT0 and handle the next
  SETUP correctly. Replaced a 4096-iteration RXPKTRDY-ack spin with
  a bounded architectural-counter wait (50 ms maximum).
- Local WSL cross-build and Android boot v2 checks PASS, tested image:
  `shim/out/cactus/cactus-trace-stage-12-boot.img`,
  28,954,624 bytes, SHA256
  `fadf08ae5cdf90b789ebd91eaa7648266c87356ba081eca4f31a3071ce97ea36`.
  Copied the exact verified image to
  `shim/out/cactus/cactus-trace-stage-12-working-boot.img`
  to preserve this hardware-positive revision.
- Fastboot serial `<redacted-device-serial>`, product `cactus`; flash and reboot
  returned OKAY. Windows enumerated `USB\\VID_1209&PID_0001`, exposed
  CDC ACM `COM25`, and opened it with DTR asserted.
- The Windows binary capture `../stage12-ep0fix-raw.bin` contains
  **256 bytes: exactly eight matching copies of the 32-byte binary
  `CLOG` frame**. File SHA256:
  `59e881197d044cfa9cbd3ea726f8161d1ce23b76602b955fd20b20a69e851322`.
  Log: `../stage12-ep0fix-usb.log`.
  **The normal USB data cable supports shim-origin raw binary transport.**
- **Remaining:** This test sends a hardcoded proof frame, not arbitrary
  shim/XNU logs. TWRP ADB did not return within 44 seconds, and a
  subsequent read-only check found neither ADB nor fastboot. USB
  disconnect/recovery cleanup may still need work. Full XNU boot and
  XNU-side USB logging remain unproven; the next milestone is to
  stream a bounded trace buffer using this working CDC transport.

### 2026-10-22 Stage 12 TX-IRQ revision: hardware test failed enumeration

- Confirmed fastboot serial `<redacted-device-serial>`, product `cactus`, and the
  Stage 12 boot image SHA256
  `672772de175a8f3a51205f163f6de7ab8e0d6533efe54680e4d9afe7f7f8bbcf`
  (28,954,624 bytes). Fastboot flash and reboot both returned OKAY.
- The 40-second Windows watcher observed `Unknown USB Device
  (Configuration Descriptor Request Failed)` and later `Unknown USB
  Device (Set Address Failed)`; no CDC COM interface appeared.
  `stage12-txirq-raw.bin` contains **0 bytes** and its exact CLOG
  binary-frame verifier reported `NO VERIFIED RAW FRAME`.
- TWRP did not reappear over ADB within 44 seconds. A later read-only
  ADB/fastboot check found no device. No `/cactus-trace.bin` was pulled.
- Captured logs: `../stage12-txirq-usb.log` and
  `../stage12-txirq-usb.err`. Phone must be returned to TWRP or fastboot
  through the established manual recovery path before another flash.
- Remaining issue: USB EP0 control-transfer and SET_ADDRESS handling;
  this latest TX-IRQ scheduling fix did **not** establish raw USB logs.

### 2026-10-22 Benesse MTK XNU source audit and cactus implementation plan

- Reviewed local Benesse device, bootshim and kernel source repositories
  under `../xnu_device_benesse_a04br3/`; actionable comparison and
  acceptance gates recorded in `BENESSE-CACTUS-PORT-PLAN.md`.
- Benesse device README documents a 2026-10-20 HFS+ RAMDisk +
  diagnostic PID 1 **hardware milestone**; this is not evidence that
  cactus runs XNU or that a full iOS/macOS environment is working.
- Highest priority cactus fixes: use the correct EL2 exception vectors
  at the initial LK handoff (current `shim/start.S` installs only
  `VBAR_EL1` there); validate live FDT DRAM/reserved memory before
  copying XNU; preserve the 4 KiB hardware granule, 16 KiB software
  VM, GICv3 and Android boot header v2.
- Source review only: no image flashed or XNU runtime milestone
  established by this audit. Stage 12 USB bulk proof remains absent.

### 2026-10-21 USB bulk raw-byte verification prepared

- **Stage 12 test attempt 2:** User's standing flashing permission
  applied. Flashed Stage 12 with vendor-correct W1C IRQ acknowledgement,
  SHA256 `6dbae7e9d68494ba9d4b9ee5804edabe9dbdf3acc295424412f79544014afedf`.
  Flash and reboot succeeded. Windows then reported
  `Unknown USB Device (Configuration Descriptor Request Failed)`,
  an improvement over attempt 1's
  `Device Descriptor Request Failed`. No COM device or binary CLOG
  data; zero bytes captured. No automatic TWRP ADB observed in 40
  seconds. Windows log
  `F:\ROM\patching\redmi 6a xnu\cactus-usb-stage12-attempt2-windows.log`,
  binary capture `...\cactus-usb-stage12-attempt2-raw.bin` (empty).
- **Control-IN completion fix built, offline only:** Added the EP0 TX
  completion IRQ condition before scheduling subsequent descriptor
  packets, preventing tight polling from submitting a second packet
  before the prior one completes. Latest `stage-12-boot.img` built and
  validated, SHA256
  `672772de175a8f3a51205f163f6de7ab8e0d6533efe54680e4d9afe7f7f8bbcf`.
  **This latest version was not flashed/hardware-tested.** No raw USB
  logging established yet.

- **Stage 13 hardware PASS:** Applied the MT6765 vendor USB2 PHY mode
  initialization before soft-connect. Device returned to TWRP
  automatically with `CTOK stage=13 status=1 state=13`, sampled
  `DEVCTL=0x99` (previous Stage 11 was `0x80`) and live RX-ready/EP0
  activity. `PHY+0x6c` changed from `0x00000000` to `0x00003f2f`;
  `PHY+0x18` from `0x00100482` to `0x40100482`. The many IRQ samples
  in the original trace count a latched interrupt repeatedly, rather
  than meaning hundreds of thousands of distinct USB resets.
  Local trace `F:\ROM\patching\redmi 6a xnu\cactus-usb-stage13.bin`.
- **Stage 12 hardware FAIL:** Rebuilt Stage 12 with proven PHY mode
  initialization and fixed its CLOG packet from 31 to 32 bytes.
  SHA256 `29fee6e20db3c470d289d0acd24acca21a7d283658caf5fb55131733eeee9ed2`
  was verified and flashed to boot. Windows watcher saw an
  `Unknown USB Device (Device Descriptor Request Failed)` and
  captured zero bytes. No TWRP ADB after 44 seconds; no Stage 12 trace
  available. The actual failure may be USB EP0 protocol/interrupt
  handling or failure to chainload recovery after USB activity.
  Watcher log `F:\ROM\patching\redmi 6a xnu\cactus-usb-stage12-windows.log`.
- **Confirmed interrupt bug in Stage 12:** MTK Linux 4.9
  `mt6765/usb20.c:807-808` writes sampled status to `MUSB_INTRTX`
  and `MUSB_INTRUSB` (write-one-to-clear). Earlier shim code only read
  these registers, so stale RESET and TX IRQs could be handled
  repeatedly and disrupt address/configuration. Patched this and
  rebuilt offline: *new* Stage 12 image SHA256
  `6dbae7e9d68494ba9d4b9ee5804edabe9dbdf3acc295424412f79544014afedf`;
  boot-image layout PASS. **Updated image NOT flashed or hardware
  tested.** Recover the handset to TWRP/fastboot before next test;
  do not flash the superseded Stage 12 binary.

- **Stage 11 hardware PASS:** Fastboot-flashed, rebooted, automatically
  returned to TWRP. Live recovery `adb pull /cactus-trace.bin` decoded
  `CTOK stage=11 status=1 state=11`, 748341 polling iterations;
  `HWVERS=e503`, initial `POWER=0x20`, sampled `POWER=0x60` but
  USB reset=0, EP0 TX IRQ=0, RX-ready=0, `DEVCTL=0x80`.
  TWRP Linux's working USB gadget has `POWER=0x71`, `DEVCTL=0x99`.
  Stage 12 CDC raw test is premature: the link never generated a USB
  reset when the shim only toggled MUSB `SOFTCONN`.
- **Stage 13 offline built:** Two-second PHY DEV_ACTIVE recovery from
  cactus vendor USB2 driver, IRQ polling, restore PHY then TWRP.
  `out/cactus/cactus-trace-stage-13-boot.img` Android boot v2 layout PASS
  SHA256 `06596288d65edc69607450bfd24e1802c30ef9049409be4c2599f8a9d6aec395`.

- Stage 7 (MUSB core + PHY read-only), 9 (MUSB C ABI/read), and
  10 (one-second soft-connect) passed on real Redmi 6A hardware and
  automatically chainloaded TWRP; corresponding `CTOK` traces were
  decoded. Stage 8 failed to enumerate or produce a recovery trace.
- Implemented **Stage 11**: two-second read-only MUSB IRQ/EP0 sampling
  while connected, then disconnect and TWRP chainload. Locally built,
  boot image validation PASS, SHA256
  `523b391a0f6ede8c041b8ed80ea772c733317c46d88e556d9d13a6af74e3db25`.
- Implemented **Stage 12**: guarded 10-second CDC-ACM probe that queues
  a fixed binary `CLOG` frame on EP1 only after host DTR; added Windows
  PowerShell binary COM capture and exact frame verifier. Locally built,
  boot image validation PASS, SHA256
  `1556a5c0b8e650b1c77657487a8356f38b65c728b0c3feb5536d57f3c3be162a`.
- **Offline only, no Stage 11/12 flash and no successful USB bulk
  transfer yet.** Run Stage 11 on device first, decode `/cactus-trace.bin`
  from its automatic recovery return; only then try Stage 12 with
  `shim/capture-usb-raw.ps1` monitoring Windows during reboot. Reference
  `shim/TRACE-USB-CACTUS.md` for exact commands and interpretation.

### 2026-10-21 MT6765 MUSB USB console groundwork — offline

- **Stage 8 hardware FAIL (21 Oct):** User-approved flash of the exact
  `9b56aca2f104dd9eaa5f53193006de8753c690bbad12ec82292db4f777e2935a`
  image succeeded and reboot returned OKAY. No `1209:0001` device
  appeared in the Windows watcher, and after 40 seconds neither ADB
  nor fastboot detected the phone. User later reported manually entering
  TWRP; no Stage 8 trace survives on a different boot image.
  **No automatic TWRP return was observed.**
- Added **Stage 9** read-only MUSB C ABI probe and **Stage 10** bounded
  one-second soft-connect-only experiment to split the failure. Built
  both offline and validated Android boot v2 headers; not flashed.
  Stage 9 SHA256 `dc257fe55de9e3d1d5d0899c33edd9c992bc7a8b23be935a175c3e0d673026f6`;
  Stage 10 SHA256 `664e287bce477c7e84028ab966d7215772d8beb5fe88f63b53d746b7115e5060`.

- **Stage 7 hardware PASS (21 Oct):** User authorized test flash. Fastboot
  serial `<redacted-device-serial>`, product `cactus` verified. Flashed the Stage 7
  USB MMIO read-only image (SHA256
  `f3119903cb319950ee210544c3ad14b0643ebb2575d10ae2250d3b234f095ba3`):
  send/write/reboot all `OKAY`. TWRP returned via ADB, and the pulled
  `F:\ROM\patching\redmi 6a xnu\cactus-usb-stage7.bin` decoded `CTOK`,
  stage 7, status 1, EL2, MUSB core `0x11200000`, HWVers `0xe503`,
  POWER `0x20` (HS enable, soft-connect clear), DEVCTL `0x80`
  (B-device), EPINFO `0x88`, RAMINFO `0x8b`, L1INTM `0`, and readable
  PHY registers from `0x11cc0800`. Read-only USB register access in the
  LK shim is proven; USB enumeration is still untested. Stage 8 next,
  with its known-good TWRP return probe and Windows PnP watcher.

- Reviewed the user's Qualcomm DWC3 `USB-SHIM-GUIDE.md`. Cactus uses the
  different MediaTek MUSB gadget peripheral, verified in live TWRP
  `musb-hdrc` sysfs/debugfs and the `mt6765.dts` + 4.9 vendor driver.
  Core `0x11200000`, PHY registers `0x11cc0800`, observed HWVers
  `0xe503`, USB clock dependencies and EP0 register layout identified.
- Stage 7: read-only USB core/PHY register snapshot into the normal
  `CTOK` TWRP-return initramfs trace. Stage 8: guarded 10-second
  MUSB PIO CDC-ACM enumeration experiment (EP0 standard + CDC controls;
  bulk EP1, interrupt EP2), then TWRP. Both isolated from XNU.
- Local builds and Android boot-v2 layout checks passed:
  `shim/out/cactus/cactus-trace-stage-7-boot.img` (28,952,576 bytes)
  SHA256 `f3119903cb319950ee210544c3ad14b0643ebb2575d10ae2250d3b234f095ba3`;
  `shim/out/cactus/cactus-trace-stage-8-boot.img` (28,954,624 bytes)
  SHA256 `9b56aca2f104dd9eaa5f53193006de8753c690bbad12ec82292db4f777e2935a`.
- Build and expected log extraction: `shim/TRACE-USB-CACTUS.md`.
  **Offline builds only; neither Stage 7 nor Stage 8 hardware tested or
  flashed in this work.** The DWC3 guide cannot be ported by copying its
  register and DMA code; after proof of enumeration, XNU will still need
  its own USB service/driver for post-handoff logging.

### 2026-10-21 Full XNU pmsg diagnostic image (offline; not yet flashed)

- Stage 6 hardware **PASS**: the shim wrote
  `CACTUS/SHIM stage 6 transport` into the native Linux ramoops pmsg zone
  at physical `0x47d60000`; TWRP exposed the marker in
  `/sys/fs/pstore/pmsg-ramoops-0`. The appended CPIO trace separately
  decoded `CTOK`, stage 6, status 1, CurrentEL `0x8` (EL2).
- Added the same pmsg trace writer to the real XNU shim with
  `TRACE_PMSG=1`: checkpoint immediately after BSS initialization,
  shim/loader milestones, and a checkpoint reached at EL1 after `eret`
  before branching to the XNU entry. This avoids the legacy CDB1 writes to
  the recovery ramoops console region in pmsg builds.
- Local Ubuntu WSL build completed and `check_image.py` passed Android
  boot header v2, cactus kernel/DTB, and SWEETXNU payload layout.
  `shim/out/cactus/shim-xnu-pmsg-boot.img`: **16,777,216 bytes**,
  SHA256 `966e3ec0707cc89f214f94029fcb7558878cfe474553b7d387d845aa29cfdc2e`.
  Disassembly confirms early pmsg call and EL2 `eret` to an EL1 pmsg call
  followed by the XNU branch. `TRACE_WDT=0` so the loader disables the
  watchdog at handoff; the user plans manual boot rollback on a hang.
- **Not flashed**. A successful Stage 6 return to recovery does not establish
  that pmsg survives a black-screen XNU hang, boot-partition rollback, or
  cold reset. Retrieve TWRP pmsg promptly after a warm recovery.
  See `shim/TRACE-PMSG-XNU.md` for build, test and extraction commands.

### 2026-10-21 Patched full XNU EL1 image flashed; startup unconfirmed

- User explicitly authorized full XNU flash. Fastboot confirmed serial
  `<redacted-device-serial>`, product `cactus`, boot size `0x4000000`.
- Flashed `shim/out/cactus/shim-xnu-el1-boot.img` (16,777,216 bytes),
  SHA256 `e69c5f165464f3cf00f87cfd05545f050ca97be4fe63f0997def6af44587c03d`.
  Fastboot Sending/Writing both OKAY; fastboot reboot OKAY.
- Two immediate read-only `adb devices -l` / `fastboot devices` checks
  showed no enumerated device. Physical screen state was not observed.
  This does not prove XNU reached its entry or diagnose its failure.
  Next: user report on display and recovery status, followed by staged
  logging to distinguish shim initialisation, loader, and XNU entry.

### 2026-10-21 Stage 5 EL2-to-EL1 HARDWARE PASS

- User authorized flashing the next Stage 5 test. Elevated fastboot verified
  serial `<redacted-device-serial>`, product `cactus`, boot size `0x4000000`.
  Image SHA256 was `36485143a88dfc05e677f84bfdb81b2fd8d03ccea153fff079446319de540b60`.
- `fastboot flash boot` reported Sending OKAY and Writing OKAY; reboot OKAY.
  TWRP appeared over ADB as `recovery product:omni_cactus`.
- Pulled 20,544 bytes from `/cactus-trace.bin` to
  `F:\ROM\patching\redmi 6a xnu\cactus-trace-stage-5.bin`.
  Decoded `CTOK`, version 1, stage 5, status 1, `CurrentEL=0x4` (EL1),
  DTB `0x47880000`, MPIDR `0x80000000`, timer 13 MHz,
  WDT mode `0x5d`, status `0`.
- This proves the EL2-to-EL1 transition executes on device and still boots
  TWRP. It does not yet prove full XNU handoff or display initialization.
  `shim-xnu-el1-boot.img` remains an offline build, not flashed this turn.

### 2026-10-21 XNU primary EL2-to-EL1 handoff (offline; hardware untested)

- Stage 3 and 4 device traces established `CurrentEL=0x8` (EL2) at cactus
  bootloader entry. The full `loader_boot()` previously jumped directly to
  XNU from that level. XNU's primary `__start` uses EL1 startup assumptions;
  the existing `0065-sweet-smp-psci.patch` switches EL2->EL1 only for
  *secondary* CPU entry. This is a strong entry mismatch, not yet a
  hardware-confirmed cause of the black screen.
- Wired cactus `loader_boot()` to `xnu_enter_el1()` in `shim/start.S`.
  It preserves boot arguments in x0, masks interrupts, configures EL2
  trap/virtualization/counter/GIC controls, prepares SCTLR_EL1 and SP_EL1,
  then `eret`s into the XNU physical entry in EL1h. Sweet keeps its existing
  direct C handoff. Fixed ICC_SRE bit ORs to valid AArch64 immediates.
- Stage 5 uses the corresponding transition then records the resulting
  `CurrentEL` in `/cactus-trace.bin` and chainloads the original TWRP.
  Local WSL cross-compile **passed**; image-check inspected the boot v2
  header and addresses. `shim/out/cactus/cactus-trace-stage-5-boot.img`
  SHA256 `36485143a88dfc05e677f84bfdb81b2fd8d03ccea153fff079446319de540b60`.
  Expected device trace after recovery: `CTOK`, stage `5`, `CurrentEL=0x4`.
- Full XNU image built **offline** from existing payload
  SHA256 `f7dadd1e3042d69ebbf4e4ebe3dea63f0b1753d7455fd2a9946a984eefa2d101`:
  `shim/out/cactus/shim-xnu-el1-boot.img`, SHA256
  `e69c5f165464f3cf00f87cfd05545f050ca97be4fe63f0997def6af44587c03d`.
  Boot v2 image checks passed; compiled disassembly confirms `loader_boot`
  calls `xnu_enter_el1` and the trampoline contains `eret`.
- **No new device flash.** First hardware test Stage 5, recover TWRP,
  pull and decode `/cactus-trace.bin`; only if EL1 (`0x4`) is confirmed
  should the full XNU image be tested. A hang after this still needs
  separate isolation of framebuffer state, watchdog programming, payload
  relocation or XNU startup. `atag,videolfb-islcm_inited=0` means the
  absence of an MI logo alone cannot establish where execution stopped.

### 2026-10-21 Stage 4 debug initialization HARDWARE PASS

- User explicitly authorized Stage 4 flash. Fastboot identified serial
  `<redacted-device-serial>` as `cactus` with 0x4000000-byte boot partition.
  Verified Stage 4 image SHA256
  `e2e78f6ccac73526a6c71d5b1c77fcaa125eb59aaf271d8a28ad62c80e14148a`.
- `fastboot flash boot` reported Sending OKAY and Writing OKAY, followed
  by successful `fastboot reboot`; ADB rediscovered the device in TWRP
  recovery as `omni_cactus`.
- Pulled `/cactus-trace.bin` (20,544 bytes) to
  `F:\ROM\patching\redmi 6a xnu\cactus-trace-stage-4.bin` and decoded:
  `CTOK`, `version=1`, `stage=4`, `status=1`, DTB x0 `0x47880000`,
  CurrentEL `0x8`, MPIDR `0x80000000`, timer 13,000,000 Hz,
  WDT_MODE `0x5d`, WDT_STATUS `0x0`, embedded `CDB1` magic.
- Extracted `cactus-cdb-stage-4.bin` and ran `tools/cactus-pstore.py`:
  CDB1 version 1, seq 0, count 0, CPU 0, flags 0, last event `ENTRY`,
  entry and last ticks both 58923749. This demonstrates that the
  actual C `dbg_init()` executed and its initialized header survives
  the shim-to-TWRP handoff and ADB extraction.
- No XNU attempted in this test. Indefinitely hung XNU still needs
  a proven reset/retention mechanism for postmortem capture.

### 2026-10-21 Stage 3 trace transport HARDWARE PASS

- User booted the Stage 3 diagnostic image to TWRP and pulled
  `/cactus-trace.bin` successfully (20,544 bytes). Host decode of
  `F:\ROM\patching\redmi 6a xnu\cactus-trace.bin` returned `CTOK`,
  `version=1`, `stage=3`, `status=1`; DTB x0 `0x47880000`,
  CurrentEL `0x8` (EL2), MPIDR `0x80000000`, CNTFRQ 13,000,000 Hz,
  WDT_MODE `0x5d`, WDT_STATUS `0x0`. The 20,480-byte CDB1 region is
  all zero, expected because Stage 3 does not call `dbg_init()`.
- This **proves** the shim can write into the appended ramdisk record and
  that this TWRP build can expose it through ADB after chainload.
  It does **not** prove XNU entry or permit reading a black-screen hang
  without a return to TWRP. Next hardware experiment: Stage 4, which
  calls `dbg_init()` and should expose the CDB1 ring in the same file.
  Stage 4 image SHA256 `e2e78f6ccac73526a6c71d5b1c77fcaa125eb59aaf271d8a28ad62c80e14148a`.

### 2026-10-21 black-screen confirmation and TWRP trace transport (offline)

- User confirmed `shim-adt-fix-boot.img` hung on a black screen after the
  successful boot-partition flash and is restoring the known-good boot.
  There was no ADB, fastboot, or captured post-hang trace. **XNU entry
  remains unproven.** No device writes were made in this follow-up.
- Added `shim/trace_initramfs.py`, `shim/build_trace_probe.sh`, and
  `shim/TRACE-BOOT.md`. Stage 3 in `shim/linux_stage_probe.S` records
  CPU/DTB/timer/read-only WDT values at a checked fixed address within a
  second raw CPIO archive appended to the **unaltered original recovery
  gzip**. The Linux 4.9 `init/initramfs.c` parser supports this concatenation.
  The stage returns to unmodified TWRP so ADB can pull `/cactus-trace.bin`.
- Stage 4 calls the real `shim/debug.c::dbg_init()`, snapshots the
  20 KiB debug RAM header/ring into the raw CPIO record, and then chains to
  TWRP. Stage 4 changes no RTC, watchdog mode, eMMC or recovery partition.
  It must only be tested after confirming the stage-3 file is populated
  on the actual device. A failed stage can still hang.
- WSL cross-compiler built both **offline** 28,952,576-byte boot v2 images.
  `check_image.py` passed header, load address, DTB and ramdisk layout.
  Stage 3: `shim/out/cactus/cactus-trace-stage-3-boot.img`
  SHA256 `be243f79365dc75aab351caa8c16f10ff10efb2025b9552ad451b5d72678220f`.
  Stage 4: `shim/out/cactus/cactus-trace-stage-4-boot.img`
  SHA256 `e2e78f6ccac73526a6c71d5b1c77fcaa125eb59aaf271d8a28ad62c80e14148a`.
  Their original TWRP Linux Image, appended FDT and original gzip stream
  are retained. **Neither image has been flashed or proven to boot.**
- Next: after the user completes ALPS recovery, run a separate, explicitly
  approved stage-3 boot-only test; if TWRP loads, `adb pull
  /cactus-trace.bin` then decode it. `CTOK` and `stage=3` establish a
  live trace channel. Only then test stage 4 for `CDB1` capture.
  Because a hung XNU does not chain to TWRP, the CPIO technique is
  **not** a black-screen postmortem recorder by itself. A watchdog
  return path or matching-kernel early snapshot remains unproven.

### 2026-10-21 ADT-fix full XNU image hardware attempt

- User explicitly authorized flash and rollback if necessary. Elevated
  `fastboot devices` returned `<redacted-device-serial>`; SHA256 of
  `shim/out/cactus/shim-adt-fix-boot.img` matched
  `1c15d40bad36d73cf4c93ec11c1161b85ee23d092890e0ced87483f094fa649b`.
- `fastboot -s <redacted-device-serial> flash boot ...shim-adt-fix-boot.img`:
  Sending `boot` 16384 KB OKAY, Writing `boot` OKAY; `fastboot reboot` OKAY.
- Two immediate post-reboot read-only polls returned empty ADB device list
  and no fastboot device. This is **not proof of XNU entry** or of a crash;
  physical display result and later USB state have not been observed.
- Recovery partition was not written. If device is black/hung, use the
  documented Power+Volume Down fastboot recovery and restore known-good boot.

### 2026-10-21 XNU payload loader ADT fix (offline only)

- Found a deterministic mismatch: `mkadt.py`/`mkpayload.py` produce an
  **Apple DeviceTree (ADT)**, while `shim/loader.c` expected Linux FDT
  `0xd00dfeed` at the payload ADT offset. The supplied payload instead has
  nine root properties, six children, and a `name` property, so the old
  loader would reject it as `ADT is not an FDT` before copying XNU.
- Replaced the incorrect FDT magic check with bounded Apple ADT root checks.
  This validates the expected root format only, not the entire nested tree.
- Cross-compiled `loader.c` using Ubuntu WSL AArch64 GCC. Rebuilt full XNU
  payload boot image `shim/out/cactus/shim-adt-fix-boot.img` (16,777,216
  bytes; SHA256 `1c15d40bad36d73cf4c93ec11c1161b85ee23d092890e0ced87483f094fa649b`).
  `check_image.py` passed Android boot v2 layout and SWEETXNU ramdisk checks.
  The existing 16,540,608-byte payload ADT root independently passes checks.
- **NOT flashed**. Earlier shim initialization (`dbg_init`, watchdog writes),
  LK-provided `/chosen` initrd addresses, segment copies, and XNU entry are
  unproven on-device. Next stage should isolate those paths with recoverable
  TWRP chainload rather than treating a black screen as XNU execution.

### 2026-10-21 staged MMIO diagnostic images (offline)

- **Stage 2 HARDWARE PASS (2026-10-21):** Verified connected unlocked
  `cactus` serial `<redacted-device-serial>` and image SHA256
  `f57e5c603c5876e46c2ee9ac08de266862e0d8141f9142a0591e329950966d6e`.
  Flashed `cactus-stage-2-boot.img` to boot: Sending OKAY, Writing OKAY;
  reboot OKAY. After eight seconds ADB detected the same device in
  `recovery` (`product:omni_cactus`). Stage 2's read-only accesses to
  MTK top-RGU MODE/STATUS completed and chainloaded TWRP successfully.
  This does not prove watchdog programming, timer expiry, or reset works.
  Recovery partition remains untouched. Next isolate the actual C
  initialization / ramdisk payload discovery, retaining TWRP handoff.

- **Stage 1 HARDWARE PASS (2026-10-21):** `fastboot devices` identified
  `<redacted-device-serial>`, `getvar product` returned `cactus`, and local SHA256
  for `cactus-stage-1-boot.img` matched
  `b04cd31ebe8c0f21160cdaa0f40ade5b3d57794cbcc80a98f615a04a31023935`.
  `fastboot flash boot` reported Sending OKAY and Writing OKAY; reboot
  OKAY. After an initial empty ADB check, another check 8 seconds later
  returned the same serial in `recovery`, `product:omni_cactus`.
  The isolated read/write/restore debug-RAM probe successfully returned
  to TWRP. This does not validate the complete C `dbg_init()` routine or
  recover persistent trace records. Recovery partition untouched.
  Stage 2 remains unflashed and untested; test separately next.

- Implemented `shim/linux_stage_probe.S`, preserving the TWRP chainload
  pattern after two independent checks. `STAGE=1` saves/writes/reads/restores
  one word at reserved ramoops `0x47c90000` and cache-cleans it; `STAGE=2`
  reads MTK top-RGU MODE/STATUS without programming the watchdog.
  Both are isolated approximations, not actual `dbg_init`/`wdt_init`.
- WSL AArch64 compiler and linker produced both images; `check_image.py`
  passed header/layout checks (v2, page 2048, kernel `0x40080000`,
  ramdisk `0x51b00000`, tags `0x47880000`). Each is 28,932,096 bytes:
  `shim/out/cactus/cactus-stage-1-boot.img` SHA256
  `b04cd31ebe8c0f21160cdaa0f40ade5b3d57794cbcc80a98f615a04a31023935`;
  `shim/out/cactus/cactus-stage-2-boot.img` SHA256
  `f57e5c603c5876e46c2ee9ac08de266862e0d8141f9142a0591e329950966d6e`.
- Neither image has been flashed; stage 1 temporarily touches shared pstore
  and a fault in either stage could leave a black screen. The only observable
  success criterion is TWRP/ADB recovery after handoff. A successful stage 2
  proves watchdog register **reads**, not safe watchdog programming/reset.
- The original entry probe already booted TWRP on-device. Next controlled
  experiment is stage 1, then stage 2 only after verifying recovery and
  rollback. Payload discovery and full XNU handoff still await isolation.

### 2026-10-21 post-entry-probe code audit

- Entry-probe hardware success narrows failure but does not test `shim_main`.
- `shim_main` first calls `dbg_init()` (writes to reserved ramoops memory
  at `0x47c90000`), then `dbg_mark()`, then `wdt_init()` (MMIO at
  `0x10007000`), all before the framebuffer and `loader_boot()`.
  Earlier TWRP pstore reads could not recover this custom trace.
- `loader_boot` depends on LK DTB `/chosen/linux,initrd-start` and
  `linux,initrd-end`, plus SWEETXNU payload validation. These are
  unverified for the actual black-screening image.
- `loader_boot` explicitly `wdt_disable()` before branching to XNU;
  an XNU hang therefore has no shim watchdog fallback. Panel state also
  makes any framebuffer-only marker inconclusive.
- Next implementation: provide independent hardware-observable stage
  checks with a known-good TWRP return path, isolating early debug RAM,
  watchdog, and loader parsing in successive variants. Do not assume
  direct `dbg_init` logging or framebuffer output is observable. Preserve
  boot/recovery rollback and validate any new image before flashing.
- Attempted read-only `adb devices -l` in this review failed to start its
  server: `could not read ok from ADB Server`; no hardware action taken.

### 2026-10-21 shim-entry isolation (source prepared)

- **HARDWARE SUCCESS (2026-10-21):** Using approved elevated Windows
  terminal access, fastboot detected serial `<redacted-device-serial>`, product
  `cactus`, `unlocked: yes`. Flashed `cactus-entry-probe-boot.img`
  SHA256 `a349e6fa619babd08d2ca3016e73116995e2b3dc3f6dca8519e88df511da5c82`
  to **boot** (`Sending OKAY`, `Writing OKAY`), then `fastboot reboot` OKAY.
  Subsequent `adb devices -l` reported the same serial in `recovery`,
  `product:omni_cactus`; `uname -r` returned `4.9.190+`.
  This proves the probe's early CPU setup and subsequent TWRP Linux handoff
  complete on device; **no XNU execution is implied**. Recovery partition
  was untouched. Follow-up: isolate `dbg_init`, `wdt_init`, and loader.

- **Build completed:** `shim/out/cactus/cactus-entry-probe-boot.img`, size
  28,932,096 bytes, SHA256
  `a349e6fa619babd08d2ca3016e73116995e2b3dc3f6dca8519e88df511da5c82`.
  Ubuntu WSL AArch64 GNU assembler/linker and MTK mkbootimg worked under
  approved elevated command. Android boot v2; kernel `0x40080000`, ramdisk
  `0x51b00000`, tags and DTB `0x47880000`, page size 2048.
- Offline verification: Linux Image magic valid; embedded original TWRP
  kernel, appended FDT, and compressed ramdisk match extracted recovery
  components byte-for-byte. Fits boot partition. Not hardware tested;
  **no flash**. Earlier WSL denial is no longer a build blocker.

- User confirmed the existing Linux chainloader already booted TWRP on-device.
  LK -> custom ARM64 code -> TWRP is proven; do not repeat that test.
- Added `shim/linux_entry_probe.S`, a second-stage witness. It preserves the
  recovery Linux chainload but first performs interrupt masking, SPSel,
  stack setup, VBAR_EL1, and BSS zeroing similar to the main shim. It avoids
  watchdog, RTC, framebuffer, pstore, XNU, and MMU setup. Successful TWRP
  boot would narrow the failure to later shim logic, with the caveat that
  the probe uses an isolated stack/vector layout.
- Source only, not built or flashed: the last WSL attempt returned
  `WSL/E_ACCESSDENIED`. Build and verify before a device experiment.
- Next after a successful probe: isolate `dbg_init()` and `wdt_init()`;
  both precede `loader_boot()` in the current cactus shim.

### 2026-10-21 follow-up: entry proof readiness

- Revalidated `shim/out/cactus/cactus-linux-chainload-boot.img` locally:
  SHA256 `2d836032015ddeec010bd90fdd991307b4a0495d9d826fe16fc151fae4e77000`;
  Android boot v2, 2048-byte pages, kernel `0x40080000`, ramdisk
  `0x51b00000`, DTB/tags `0x47880000`. Embedded original recovery Linux
  Image, appended FDT, and recovery ramdisk compare byte-identical to
  extracted recovery components. This is offline evidence only, **not**
  evidence of a successful hardware chainload.
- `fastboot devices` returned no connected device in this run. No flash.
- The attempted WSL source inspection returned `WSL/E_ACCESSDENIED`; LK/
  kernel ownership and warm-reset persistence of watchdog `NONRST_REG2`
  remain unverified. Do not use `cactus-wdt-smoke-boot.img` on hardware.
- Next smallest device experiment, once connected and recovery can be
  restored: test the existing **watchdog-free** Linux chainload image to
  establish LK -> custom ARM64 code -> unmodified TWRP Linux. If it boots
  TWRP, that proves early custom-code execution and Linux handoff, but does
  not establish XNU entry or early trace persistence. Observe ADB recovery
  and retain the known-good ALPS boot and original recovery partitions.
- Only after this works, separately validate top-RGU watchdog behavior
  without relying on an unproven persistent register marker.

### 2026-10-21 standalone Linux chainload probe

### 2026-10-21 watchdog recovery review and implementation sequence

- **2026-10-21 offline WDT prototype:** `shim/linux_wdt_probe.S` compiled under
  WSL and packaged as `shim/out/cactus/cactus-wdt-smoke-boot.img`, size
  28,932,096 bytes, SHA256 `913fd46341059fc07c306053d30f4e22af56563c9e81f1c36dd29b6fec4c6bac`.
  Its original TWRP Linux Image, appended DTB, and compressed ramdisk verified
  byte-identical to extraction, with Android boot header addresses correct.
- **NOT READY TO FLASH:** prototype uses full `MTK_WDT_NONRST_REG2` at
  `0x10007024` as a marker, which may conflict with bootloader/watchdog stage
  bits and is not verified to survive a watchdog reset. Before deployment,
  inspect register ownership in LK/kernel and implement a masked, proven
  marker or safer fallback mechanism. If marker is lost, this image could
  trigger a reset loop. Current working TWRP chainloader remains installed.
- TWRP ADB was observed in `recovery`, but `/dev/watchdog*` and
  `/sys/class/watchdog` were not exposed by its Linux environment. No reset
  was requested on the actual device.

- **Source-confirmed blocker:** `shim/loader.c` calls `wdt_disable()` just
  before branching to XNU. The existing shim therefore has no armed top-RGU
  watchdog after handoff. A black-screen hang cannot be expected to reset.
- **Source-confirmed secondary blocker:** `shim/watchdog.c::wdt_sw_reset()`
  invokes `bootmode_prepare_reset()` before the WDT software-reset register;
  the RTC/PWRAP path previously hung. Separate the raw WDT reset primitive
  from boot-mode selection and do not call RTC from the early recovery path.
- Cross-checked register offsets and keys with the cactus 4.9 source at
  `/home/<local-user>/cactus-4.9/drivers/watchdog/mediatek/wdt/common/wdt_v2/`:
  MODE+0, LENGTH+4, RESTART+8, STATUS+0xc, SWRST+0x14,
  LENGTH `(seconds << 11) | 8`, MODE key `0x22000000`, RESTART `0x1971`,
  SWRST `0x1209`. These agree with the current shim's constants; **register
  definitions alone do not establish that the watchdog reset works on-device**.
- **Implementation plan:** (1) add a separate shim-only `cactus-wdt-smoke`
  target with no XNU, RTC/PWRAP, framebuffer or pstore accesses, and a pure
  top-RGU timeout path; (2) validate registers and emitted instructions
  offline; (3) test on-device with physical-key/ALPS recovery available and
  confirm reboot using host USB reconnect plus Linux reset-status readback;
  (4) if successful, add a reliable TWRP-capable boot dispatcher with a
  persistent attempt flag, testing fallback without XNU; (5) only then keep
  WDT armed across XNU entry and add a progress acknowledgement strategy.
- **No watchdog reset, marker persistence, or automatic TWRP fallback has
  been demonstrated on cactus yet.** No device flashing during this review.

- Source: `shim/linux_chainload.S`; read-only recovery extraction helper:
  `shim/build_linux_chainload.py`. This is separate from the XNU shim.
- Input: `../recovery.img` SHA256 `b05c6e25bf2cc2a8059684ba9af6e8ea998860ed832960ed0be61dced9861029`.
- Built using WSL AArch64 GNU tools and the existing MTK mkbootimg. Output:
  `shim/out/cactus/cactus-linux-chainload-boot.img` (28,932,096 bytes),
  SHA256 `2d836032015ddeec010bd90fdd991307b4a0495d9d826fe16fc151fae4e77000`.
- The gzip-decompressed chain image is 31,682,724 bytes: a 64-byte ARM64
  header, byte-identical 31,682,568-byte TWRP Linux Image, and tail code.
  Tail code copies Image down by 64 bytes, preserves `x0` DTB pointer, and
  branches to original Linux entry. The original appended DTB (110,891 bytes)
  and compressed TWRP ramdisk (17,591,904 bytes) match extraction byte-for-byte.
- Header uses Android v2 for **boot partition** with stock cactus DTBO,
  kernel `0x40080000`, ramdisk `0x51b00000`, pagesize `2048`; packed image
  validated offline. The GNU disassembly of tail copy/branch was inspected.
- **NOT FLASHED / NOT DEVICE TESTED**. It is still possible LK's entry rules,
  load/decompress behavior or TWRP's boot-mode assumptions will prevent boot.
  Preserve existing recovery partition and known-good ALPS `boot.img`.

- **2026-10-21 early TWRP snapshot implementation:** Added
  `tools/recovery_trace/cactus_early_snapshot.c` (built-in early-initcall
  copy of trace RAM; late proc export) and guarded integration script
  `install_early_snapshot.py`. The local Xiaomi kernel source reports
  4.9.77, while original TWRP is 4.9.190+; therefore do not repack a recovery
  using the local kernel without matching-source verification and actual boot
  tests. This source is **not** a working flashable recovery image. Retention
  and early capture have not been tested on hardware. Original recovery
  remains unchanged.

- **2026-10-21 device readiness check:** `fastboot devices` in the approved
  device context sees the Redmi 6A; `getvar product` = `cactus`, `unlocked` =
  `yes`, boot/recovery partition size `0x4000000` each. `getvar current-slot`
  returns `FAILED (remote: 'GetVar Variable Not found')`; this variable is not
  available on this bootloader. Shim trace-audit image (237568 bytes) fits.
  Boot header v2, page 2048, kernel at 0x40080000, ramdisk at 0x51b00000,
  tags/DTB at 0x47880000, DTB size 0x1b16b and command line agree with the
  reference `../boot.img`. The diagnostic image has no XNU payload. **Not
  flash-ready for a logging experiment**: TWRP has no verified way to recover
  retained trace, and `_start` can still fail before its first C trace.
  Preserve working recovery and ALPS boot; no partitions were flashed.

- **2026-10-21 instrumentation patch (local only):** Added `fault regs`
  (exception vector slot, ESR_EL1, ELR_EL1) and `fault addr` (FAR_EL1,
  SPSR_EL1, LR) persistent entries *before* framebuffer printing in
  `shim/shim.c`. Added `before wdt` (CurrentEL, DTB pointer, MPIDR) and
  `after wdt` stage markers surrounding watchdog initialization. The
  `before wdt` event uses `dbg_mark` directly to avoid watchdog ping.
  WSL `BOARD=cactus TAG=trace-audit ./build.sh` succeeded including host
  console test and image checker; resulting isolated **shim-only** diagnostic
  image `shim/out/cactus/shim-trace-audit-boot.img` is 237568 bytes, SHA256
  `9ca555f779c8ed3f5a9f3f9c75a501214be495f8a8fef365c261a2a7746d61cf`.
  This does **not** prove shim execution, memory retention, or safe boot;
  recovery logger is still unvalidated. Header checker reports Android v2
  with appended kernel FDT and DTBO blob in the separate DTB field: verify
  against the working boot image before any device flash. No phone flash.

- **2026-10-21 black-screen logging audit:** Added
  `tools/recovery_trace/LOGGING_OPTIONS.md` documenting actual instrumentation
  reach, persistent-memory collision, and ranked UART/early-recovery snapshot
  alternatives. No reliable logger is device-verified: `ENTRY_PROBE=1` never
  reaches `dbg_init`, and the current C trace cannot diagnose `_start` faults.
  Do not flash until a known-good UART capture or isolated early-recovery
  snapshot + warm-reset retention test is validated.

- **2026-10-21 recovery reader progress:** Confirmed original `../recovery.img`
  is Android boot v1, SHA256 `b05c6e25bf2cc2a8059684ba9af6e8ea998860ed832960ed0be61dced9861029`.
  Decompressing its gzip kernel reveals Linux `4.9.190+` compiled by
  `dinolek@ArchPC` on 2021-08-15. Local WSL cactus-4.9 tree has no `.config`
  or generated `utsrelease.h`, so ABI-compatible TWRP module build is **not
  established**. Embedded IKCONFIG is present and extractable; it reports
  `CONFIG_MODULES=y` and `CONFIG_MODVERSIONS=y`, so matching symbol CRCs are
  necessary. Added read-only snapshot module source and operator cautions
  under `tools/recovery_trace/`, capturing 0x5000 bytes from the trace base to
  `/proc/cactus_trace`. Not compiled, loaded, or flashed. Because recovery
  ramoops shares the address, loading after driver initialization may be too
  late; a kernel-integrated early snapshot is the likely reliable solution.
  Original recovery image remains untouched. Next: identify the exact matching
  TWRP kernel source/config and inspect ramoops probe ordering before building
  an isolated test recovery image; do not infer shim entry from black screen.
  **Capture limitation:** `ENTRY_PROBE=1` and `RESET_ONLY=2` never call
  `dbg_init`, so they produce no trace even if executed. Standard shim builds
  only start tracing after `shim_main` reaches `dbg_init`. Restoring boot then
  rebooting to TWRP offers no guaranteed DRAM retention or pre-ramoops access;
  it cannot be advertised as an automatic black-screen logger yet.

- **2026-10-21 logging decision:** Live `adb logcat` from TWRP cannot observe
  our replacement bare-metal shim, because TWRP is not running concurrently.
  Checked `/home/<local-user>/cactus-4.9/arch/arm64/configs/cactus_defconfig`:
  `CONFIG_MODULES=y`, `CONFIG_PSTORE_RAM=y`, but `CONFIG_DEVMEM` is unset.
  That explains the observed `/dev/mem` ENXIO. Existing shim tracing writes
  ad-hoc records into 0x47c90000, which recovery's ramoops driver also
  claims; `/sys/fs/pstore` will not decode that custom header. Preferred
  next experiment: first verify LK handoff and create a **dedicated read-only
  recovery trace reader** (matching the running TWRP kernel, or build into a
  test recovery kernel) which can snapshot reserved trace RAM via a debugfs/
  proc file before ramoops overwrites it. Verify warm-reset retention and
  avoid overwriting active ramoops metadata. No direct eMMC writes, RTC
  bootmode hacks, or simultaneous-TWRP live logging.

- **2026-10-21 entry-probe outcome:** User observed immediate black screen
  after flashing entry probe, then independently restored the boot image and
  returned the device to fastboot. Flash/reboot succeeded but **shim entry
  remains unproven**. Do not repeat framebuffer-only probes as evidence:
  the LK display may be off. Read-only disassembly verified ARM64 Image header
  branches to `_start` at offset 0x800; `_start` sets SP/VBAR_EL1, clears BSS,
  then calls shim_main. The unconditional VBAR_EL1/EL1 assumptions still need
  verification against cactus LK. The kernel DTS uses
  `console=ttyS0,921600n1`, provides UARTs at 0x11002000/0x11003000,
  and kernel display code respects `atag,videolfb-islcm_inited`.
  Next: establish actual LK handoff EL and UART/display state from LK/kernel
  source or instrument a recoverable test with a real observable output
  channel; avoid another blind flash and do not yet attempt XNU.

- **2026-10-21 entry-probe device test:** USB fastboot serial `<redacted-device-serial>`
  detected with elevated device access. Verified probe image SHA256
  `74f11112804ce362670517f704b1e170bfea13d35cd693e0c7f88f28f51a32b8`.
  `fastboot flash boot shim/out/cactus/shim-entry-probe-boot.img` returned
  Sending OKAY / Writing OKAY; `fastboot reboot` returned OKAY. The image
  contains framebuffer magenta/cyan entry probe and then spins, no XNU.
  **Screen observation not yet supplied**; do not infer success from flash.
  ALPS restore may be required after the test.

- **2026-10-21 focused probe:** Added `ENTRY_PROBE=1` to `shim/build.sh` and
  `shim/shim.c`. This builds a shim-only magenta/cyan framebuffer fill and
  halts before `dbg_init`, watchdog, pstore, RTC, and XNU loader. It assumes
  LK left the display active; an invisible pattern proves nothing by itself.
  WSL cross-build and image check succeeded for
  `shim/out/cactus/shim-entry-probe-boot.img` (237568 bytes), SHA256
  `74f11112804ce362670517f704b1e170bfea13d35cd693e0c7f88f28f51a32b8`.
  This image is unflashed and contains no XNU. The extracted DTS gives UART
  register ranges `0x11002000` and `0x11003000`, but pinmux and clocks are
  unproven, so neither is safe as an immediate raw UART output target.
  The DTS `/memory` node declares only `0x40000000..0x60000000`, while the
  payload assumes `0x70000000..0xc0000000`. Validate the *live* populated
  memory map and dynamic reserved-memory carve-outs before any XNU copy.

- **2026-10-21 WSL inspection and build:** WSL Ubuntu works with approved
  execution. Kernel sources at `/home/<local-user>/cactus-4.9`, extracted device
  tree at `/home/<local-user>/cactus-kernel/pack/cactus.dts`. That FDT defines
  CPU MPIDR regs `0,1,2,3,0x100,0x101,0x102,0x103`, two clusters of four.
  Actual enabled core count still needs live confirmation. Fixed cactus
  mapping in `tools/mkadt.py` and shim `test_smp()`; retain `cpus=1`.
  Rebuilt shim-only `shim/out/cactus/shim-topology-check-boot.img`, 237568
  bytes, SHA256 `e54fddb1417f5741f9a5f37473fcf39cc097c36af905633a52b8e9d2ec509861`.
  `check_image.py` passed; it contains an empty diagnostic ramdisk, not XNU.
  Fresh cactus ADT generated (`/tmp/cactus-topology-check.adt`, 9760 bytes),
  warning: optional D22AP `/product` source missing. No image was flashed.

- **2026-10-21 review:** `../issue.txt` supersedes the earlier 2026-10-20
  bootmode instructions below. On cactus, `fastboot boot IMAGE` boots the
  installed boot partition; do NOT use it to test the named image. Current
  installed image is `shim-xnu-boot.img`, observed black-screen only; this
  does not establish that LK entered the shim, because the panel may be off.
  TWRP root ADB works, but `/dev/mem` pstore access returned ENXIO.
  `shim/build.sh` now defaults `AUTO_BOOT_MODE=off`; RTC fastboot selection
  requires explicit opt-in. `EARLY_PSTORE=1` and `EARLY_WDT=1` are independent
  opt-ins, both off by default. `RESET_ONLY=2` first tries PSCI reset without
  touching MTK MMIO; watchdog reset remains a fallback if PSCI returns.
  Existing generated boot images have NOT been rebuilt or reflashed.
  CACTUS ADT and machine config currently encode 8 CPUs and SWEET-style
  MPIDRs; check actual Redmi 6A DTB/MPIDRs (likely only 4 CPUs) before an
  SMP-capable hardware run. Next step is isolated entry instrumentation and
  validation of a recovery-capable boot test, with user approval for flashing.
  Local ALPS `../boot.img` contains eight `cpu@000`..`cpu@103` node names
  (two groups of four), but node presence alone cannot establish which CPUs
  are populated/enabled. It contradicts the ADT's SWEET-style `core << 8`
  mapping and requires decoding each node's `reg`/`status` from the live DTB.

- Branch `cactus-phase1`; CI builds CACTUS via `gh workflow run xnu.yml --ref cactus-phase1 -f machine_config=CACTUS`.
- **2026-10-20 bootmode/logging handoff (latest):** full RTC bootmode writes still fail. Flashed `shim-diag-boot.img` builds `eb070b7d...` and `de159025...` (237568 bytes) both left a black screen, no fastboot/PnP (20-45 s waits). Device needs ALPS `boot` restore via SP Flash before the next test; `fastboot devices` is empty. CBUSY polling did not fix it.
- **pstore/DTB layout measured:** `pstore-reserved-memory@47c90000` size `0xe0000`; separate `ram_console-reserved-memory@47c80000` size `0x10000`; no `ramoops` node in the CACTUS DTB. The shim trace starts at `0x47c90000` (+0x1000 ring); it is not expected to appear in Android `/sys/fs/pstore` without a reader. Region is DRAM-backed: a warm reset may preserve it, a battery pull/SP Flash power cycle may not.
- **New staged diagnostic images (not flashed yet):** `shim/out/cactus/shim-reset2-boot.img` SHA256 `221aa4fcf0601690e7ee466b1996ca982dc11dfa7660a7789e878fb1d2333f3d` = pure reset (no pstore/console/timer); `shim-reset-boot.img` `2a3da61d...` = pstore + reset after 3 s; `shim-probe-boot.img` `68a14428...` = RTC_PDN1 read only + diag + reset (no RTC write); rebuilt `shim-diag-boot.img` `6817e048...` = normal diag, no startup probe, RTC write only at reset. Test order: reset2 -> reset -> probe -> diag, only after ALPS recovery and user approval. Use `fastboot boot` for reset2/reset/probe (a temporary image resets back into the untouched ALPS boot; flashing reset2 would loop forever); flash `shim-diag-boot.img` only for the final auto-fastboot test.
- **Trace tooling:** `tools/cactus-pstore.py` decodes the raw pstore trace (256 entries now); `tools/cactus-pstore-pull.sh` dumps `0x47c90000/0xe0000` via `/dev/mem` from TWRP/rooted ADB. Extraction path still needs to be chosen; no TWRP/root confirmed for this device yet.
- Zone-map fix verified in QEMU (`out10`): `kmem_range_init` no longer panics; 24 GiB zone claim; `VM bootstrap done`.
- LSE atomics fixed: CI `39377997445` success; Cortex-A53 libfirehose built with `-mno-outline-atomics`; new kernel has **0** real LSE in `__TEXT_EXEC`.
- **16K/4K pmap panic fixed (2 parts):** `0084-cactus-16k-pte-align.patch` clamps 4K-aligned `v`/`pa` in `pmap_enter_options_internal`; `0085-cactus-commpage-16k-align.patch` aligns the randomized commpage text VA and `pmap_update_tt3e()`.
- **Gotcha:** `TEST_PAGE_RATIO_4` is `(PAGE_RATIO == 4)` and not a preprocessor constant. An `#if TEST_PAGE_RATIO_4` silently compiled the first 0085 as a no-op; use a runtime `if` like 0084. Verified in disassembly (`and x25,x9,x19` before the commpage text mapping).
- CI `39385719813` success (head `07b2b85`); artifact `C:\Users\<local-user>\AppData\Local\Temp\opencode\cactus-run-39385719813\kernel.development.cactus`.
- QEMU: `out28`, `out29`, `out31`, `out32` boot cleanly to the `Apple_HFS` IOMedia wait; `out30` panicked in the unrelated interrupt watchdog (`machine_routines_common.c:893`, IRQ handler took 23 ms) under host load. No `write_pte_fast` panic remains.
- Next: boot further with an HFS ramdisk / real IOMedia, then move to device bring-up.
- Prior runs: `39374239912` success (zone fix), `39376030150` failed (script path), `39376538002` success (still LSE), `39382038752` success (0084 only; commpage panic remained).
