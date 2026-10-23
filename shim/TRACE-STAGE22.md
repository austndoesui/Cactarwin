# Combined Stage22 / Stage27 — deeper XNU startup and cactus census

**Hardware Stage22 checkpoint PASS (2026-10-23).**

Device `<cactus-fastboot-serial>`, `fastboot getvar product: cactus`. Fastboot
`flash boot` and `reboot` returned OKAY. COM25 capture:
`F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-134531-452\session-001-134634-496.bin`.
The strict decoder with `--require-stage22` returned
`STAGE22 DEEPER XNU C CHECKPOINT PASS`: 3,024 bytes, 54 valid CRC frames,
zero bad frames, unmatched bytes, sequence gaps, duplicates or regressions.
Actual ESR_EL1=`0xf2000e24` (deliberate BRK #0xe24),
ELR_EL1=`0xfffffff007ac0c0c` (deeper C checkpoint), FAR_EL1=0.
`SCTLR_EL1=0x34d4593d`; `TCR_EL1=0x22a5192519`;
`VBAR_EL1=0xfffffff007429000`;
`TTBR0_EL1=0x8117a000`, `TTBR1_EL1=0x8117b000`.
EL2 logger classified milestone=1, patch/vector/HVC restoration `(1,1,1)`,
and `0x20ff`/`0x19ff` completion statuses were 0. Read-only CPU census
MIDR=`0x410fd034`, MPIDR=`0x80000000`; CNTFRQ=13,000,000 Hz;
boot memory physical base=`0x80000000`, size=`0x1e000000`.
Device returned to fastboot after its test loop; `fastboot oem reboot-recovery`
returned OKAY, and ADB confirmed `recovery product:omni_cactus device:cactus`.

Output: `shim/out/cactus/cactus-trace-stage-27-boot.img` (45,504,512 bytes)

SHA256: `5422a4f2dc6fe0f18cafd88127a4e54f270a32342e9cdb0926a6c014a3151ea2`

The already verified Stage26 reaches the initial XNU high-VA C prologue
with MMU enabled. Stage27 carries forward its two reversible virtual
EL1t/EL1h synchronous-vector HVC stubs and USB COM logger. The BRK target
advances from `0xfffffff007ac061c` to `0xfffffff007ac0c0c` (physical
`0x81ac0c0c`, original `adrp x1` opcode `0xb0ffacc1`, replacement BRK
`#0xe24`). This allows XNU to execute early calls at `0x81ac0624`,
`0x81ac0628`, its relocation/header branch paths and further startup
calls before the later checkpoint. Any preceding synchronous exception
is recorded through the virtual EL1t/EL1h stub and EL2 physical stack.

The shim pins startup opcodes in both the Python payload verifier and
runtime before patching. All instructions and exception stubs are
restored after an EL2 HVC return. Non-synchronous faults or hangs cannot
be captured by this path and may require manual return to fastboot.

Read-only EL2 preflight logs three cactus-specific records:

* `0x2200`: MIDR_EL1, MPIDR_EL1, CurrentEL=0x8. The third field is
  the exception level, **not a CPU core count**.
* `0x2201`: CNTFRQ_EL0, CNTPCT_EL0, CNTVCT_EL0. This samples the
  preconfigured timer, **not an interrupt or scheduler test**.
* `0x2202`: loaded `boot_args` physical base, memory size and
  `topOfKernelData`.

Events `0x2203` = dual vectors, `0x2204` = patch armed, `0x2205` =
preflight, `0x2206` = EL1 ESR/ELR/FAR, `0x2207` = MMU regs, `0x2208` =
translation regs, `0x2209` = HVC origin and EL1 saved state,
`0x220a` = 1/later checkpoint, 2/earlier exception, 3/other BRK,
`0x220b` = restoration. Existing `0x20a3`, `0x20ff`, `0x19ff`
still report EL2 return and final status.

Validate a future COM25 capture with:

```powershell
python shim/decode-cactus-usb.py 'CAPTURE.bin' --require-stage22 --show 60
```

The decoder distinguishes `STAGE22 DEEPER XNU C CHECKPOINT PASS`
from `STAGE22 FIRST XNU EXCEPTION CAPTURED`. The checkpoint was verified
on hardware. It does **not** establish
working GIC interrupts, timer interrupts, secondary CPUs or XNU kernel
startup completion. CPU topology and interrupt controller registers
must be verified from cactus DTB before attempting interrupt enable/SMP.

Build/test evidence: local WSL `./build_trace_probe.sh 27` passed,
boot-v2 image check passed, six combined Stage21C2/21D/22 positive,
negative and mutation tests passed. The linked AArch64 ELF contains
real `bl stage19_probe` and `bl stage20_el1_transition` calls.
Hardware test flashed only the boot partition; recovery is working.
