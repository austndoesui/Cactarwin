# Stage21-D / Stage26: high-VA C entry and synchronous-fault capture

**Hardware test PASS on 2026-10-23 (high-VA C checkpoint).**

COM25 capture: `F:\ROM\patching\redmi 6a xnu\usb-captures\stage-cactus-20261023-130759-533\session-001-130859-702.bin`.
Strict decoder `--require-stage21d`: `STAGE21-D HIGH-VA C CHECKPOINT PASS`.
51 valid frames (2,856 bytes), zero corruption/gaps/duplicates.
Captured ESR_EL1=`0xf2000e23`, ELR_EL1=`0xfffffff007ac061c`,
SCTLR_EL1=`0x34d4593d`, VBAR_EL1=`0xfffffff007429000`,
TCR_EL1=`0x22a5192519`, TTBR0=`0x8117a000`, TTBR1=`0x8117b000`.
Stage26 logger classified checkpoint=1; vector, target and HVC patch
restoration reported `(1,1,1)` and both completion statuses were zero.
The phone produced repeated complete COM25 sessions after reboot.
ADB and fastboot did not enumerate at the first post-test check, so
After the test the device was visible in fastboot. The corrected
`fastboot oem reboot-recovery` returned OKAY, and `adb devices -l`
confirmed `<cactus-fastboot-serial> recovery product:omni_cactus device:cactus`.
**TWRP recovery confirmed.**

Image: `shim/out/cactus/cactus-trace-stage-26-boot.img`

Size: 45,504,512 bytes.

SHA256: `af28b4019764e7d55ede1c83c96f88ac40fb568f886f1f588affde915dc5a09a`.

Stage21-C2 already passed on cactus: SCTLR MMU bit enabled, first
translated XNU instruction at physical `0x8142f490`, virtual EL1t
exception vector, HVC to the EL2 physical-stack logger, and recovery.
Stage21-D extends XNU execution into the first high virtual-address
C function at `0xfffffff007ac05dc` (physical `0x81ac05dc`).

The guarded Stage26 BRK #0xe23 is placed at physical `0x81ac061c`,
virtual `0xfffffff007ac061c`, original opcode `0xa903ffff`.
This checkpoint follows multiple stack saves plus a load and store.
An earlier synchronous exception is a useful alternative outcome.
Before the XNU jump, the shim verifies exact prologue bytes,
MMU initialization, the existing EL1t vector and new EL1h vector.

The shim patches and later restores both physical backing slots of
the virtual synchronous exception table:

- EL1t at `0x81429000` (virtual `0xfffffff007429000`);
  original `0x14000400`.
- EL1h at `0x81429200` (virtual `0xfffffff007429200`);
  original `0x140003f6`.

Both execute `HVC #0x21` without using XNU's possibly invalid stack.
At EL2, the existing USB CLOG logger collects `ESR_EL1`,
`ELR_EL1`, `FAR_EL1`, `SPSR_EL1`, `VBAR_EL1`,
`TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1`,
`SCTLR_EL1`, `ESR_EL2`, `ELR_EL2` and `SPSR_EL2`.

Event markers: `21e6` dual vectors, `21e0` BRK armed, `21ea`
USB preflight, `21e1` original EL1 ESR/ELR/FAR, `21e2` MMU
state, `21e3` translation state, `21e4` HVC origin,
`21e5` classification (1=checkpoint, 2=earlier exception),
`20b3` HVC, `20a3` EL2 return, `21e8` restoration,
`20ff` and `19ff` completion.

Strict USB validation:

```powershell
python ios-cactus/shim/decode-cactus-usb.py CAPTURE.bin --require-stage21d
```

Verdicts distinguish **STAGE21-D HIGH-VA C CHECKPOINT PASS**
from **STAGE21-D EARLY XNU EXCEPTION CAPTURED**. If the phone never
reaches a patched synchronous slot, only preflight frames may arrive:
the diagnostic gate then fails, and the device may need manual
fastboot recovery. An offline build does not prove either verdict.

Local build: `./build_trace_probe.sh 26` from WSL Ubuntu inside the
`shim` directory. Offline payload pin checks and six targeted
Stage21 tests passed; ELF relocation `R_AARCH64_CALL26 stage19_probe`
and a real branch to `stage20_el1_transition` were verified.

For a later authorized device run, start `watch-cactus-usb.ps1`
before the reboot. Cactus fastboot recovery command:
`fastboot oem reboot-recovery`.
