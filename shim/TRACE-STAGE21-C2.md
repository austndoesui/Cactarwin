# Stage21-C2 / build Stage25: first post-MMU EL1t instruction

**Hardware PASS on 2026-10-23**; the first build exposed an SCTLR
readback difference, which was corrected and retested.

Image: `shim/out/cactus/cactus-trace-stage-25-boot.img` (45,504,512 bytes)
SHA256: `489f6624e18ceca84d1f347bbf4c2cef6946fbde263e55b11410ff7f301012c5`.
Stage23 and Stage24 images are preserved.

Verified device: `<cactus-fastboot-serial>`, `product: cactus`. Fastboot
send/write/reboot all returned OKAY. Live Windows COM25 capture:
`../usb-captures/stage-cactus-20261023-121212-770/session-001-121315-932.bin`;
2,912 bytes, 52 CRC-valid Stage25 frames, zero corrupted frames,
missing sequence numbers or unmatched bytes. Strict
`--require-stage21c2` returned
`STAGE21-C2 POST-MMU EL1t VIRTUAL-VECTOR GATE PASS`.
After the test, `fastboot oem reboot-recovery` returned OKAY and
ADB verified TWRP (`recovery product:omni_cactus`).

The initial image SHA256 was
`4e3499523a415e38fa055434ec31e70ae708c75dad210ef814537462079ed623`.
Three independent initial COM25 captures reached the post-MMU BRK
and virtual HVC, but produced completion status `0x511` because
the probe assumed SCTLR readback `0x3454593d`.
The actual cactus CPU read back `0x34d4593d` (bit 23 set);
the static XNU write constant remains unchanged. The corrected probe
expects the measured value and finished with zero-status restoration.

Stage24 demonstrated that XNU reaches `0x8142f474` in EL1t before
enabling its MMU, and measured the 4 KiB translation tables. Stage25
pins the exact original instruction bytes using
`check_stage21c2_entry.py`. It arms `BRK #0xe22` at
`0x8142f490`, the first instruction *after* the MMU enable
`MSR SCTLR_EL1,x0` at `0x8142f488` and `ISB` at `0x8142f48c`.
The original instruction at the trap site is `0xd2a00600`.

While still EL2 with its own MMU off, the shim patches the **physical**
backing of XNU's post-MMU EL1t synchronous vector at `0x81429000`.
Its corresponding **virtual** VBAR is `0xfffffff007429000`.
The temporary vector executes direct `HVC #0x21` and uses no
virtual EL1 stack accesses before entering EL2. EL2 captures:
`ESR_EL1`, `ELR_EL1`, `FAR_EL1`, `SPSR_EL1`,
`VBAR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`,
`MAIR_EL1`, `SCTLR_EL1`, and selected live descriptors.
After returning to the EL2 logger it restores the original code and
vector words and chainloads TWRP. A 5.5-second USB enumeration interval
precedes the XNU jump, allowing partial failure evidence.

Required Stage25 event sequence (56-byte CRC-protected CLOG frames):

- `0x21d6`: patch physical vector `0x81429000`, VBAR VA
  `0xfffffff007429000`, first opcode `0xd4000422`.
- `0x21d0`: armed BRK target `0x8142f490` for XNU entry `0x8142d488`.
- `0x21da`: USB preflight before ERET.
- `0x20a1`: shim-to-XNU EL1 entry.
- `0x21d1`: BRK ESR immediate `0xe22` at `0x8142f490`.
- `0x21d2`: **virtual VBAR**, TCR, `SCTLR_EL1=0x34d4593d`
  as read back on cactus (XNU wrote `0x3454593d`; hardware sets bit
  23). Bit M is set. This proves MMU enable and instruction execution
  after ISB when paired with the breakpoint record.
- `0x21d3`, `0x21d4`, `0x21d8`: translation registers and
  actual root/block descriptors.
- `0x21d5`: HVC origin is **virtual** vector VA + 4;
  `SPSR_EL2.M=5` (EL1h) while `SPSR_EL1.M=4` (interrupted EL1t).
- `0x20b3`, `0x20a3`, `0x21d7`, `0x20ff`, `0x19ff`:
  HVC, EL2 return, restored patches, zero-status completion.

From `ios-cactus/shim` under elevated local WSL Ubuntu:

```sh
./build_trace_probe.sh 25
python3 -m unittest -v test_stage21c2_entry test_stage21c2_usb
```

For a device test, start `watch-cactus-usb.ps1` **before** the
fastboot reboot, then decode each saved session:

```powershell
python shim/decode-cactus-usb.py <capture.bin> --require-stage21c2
```

The hardware pass verdict is
`STAGE21-C2 POST-MMU EL1t VIRTUAL-VECTOR GATE PASS`.
This proves the first translated XNU instruction and vector exception;
it does not prove sustained XNU kernel initialization.
For recovery from fastboot on cactus use
`fastboot oem reboot-recovery`.
