# Stage 20-B: first actual XNU instruction

Status: **local build candidate only (2026-10-22); not flashed or proven on hardware.**

**Hardware validation updated 2026-10-22: PASS.** Flashed the
SHA256-pinned image to verified cactus serial <cactus-fastboot-serial>.
The USB recording at
`../usb-captures/stage-cactus-20261022-174227-147/session-001-174229-019.bin`
contained 43/43 CRC-valid events (zero sequence gaps). Event
`0x20b2` reported EL1 ESR 0xf2000b20, ELR 0x8142f0ec,
proving the XNU branch was executed. HVC `0x20b3`, restore
`0x20b4 (1,1,1)`, and both zero-status DONE markers completed.
Decoder `--require-stage20b` passed, and ADB verified TWRP
recovery `<cactus-fastboot-serial>`. The test continues to establish only
the first XNU branch instruction, not subsequent kernel startup.

Offline image:
F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\out\cactus\cactus-trace-stage-21-boot.img

- Size: **45,504,512 bytes** (Android boot header version 2, validated).
- SHA256: **979a54dead1dbaa729f4d0a82269cda538f8dbfd8baa5856527015aba0aedd9c**.
- Local regression checks passed: one mutated-payload admission test,
  nine USB decoder tests and one native CLOG ring test.
- Crosscompiled AArch64 disassembly confirms the real entry BR x20,
  HVC #0x21, and dc cvau/ic ivau cache synchronization for the
  temporary breakpoint and its restoration.

Stage20-A confirmed that cactus can enter a shim-owned EL1 handler,
return via HVC to EL2 and chainload TWRP. Stage20-B reuses that path
to execute exactly the first instruction of the installed XNU payload.

The packed SWEETXNU first instruction is A64 0x14000719, a direct
branch from physical 0x8142d488 to 0x8142f0ec. The original
instruction at the destination is 0xd510109f. Both the offline
builder and live loader verify these exact opcodes and addresses;
a changed payload is rejected.

After the verified real XNU segment, ADT and boot_args copy, the shim
saves the destination instruction and temporarily replaces it with
BRK #0xb20 (0xd4216400), then invalidates the instruction cache.
The established EL1 trampoline logs its entry checkpoint, passes the
physical boot_args in x0 and jumps to the **actual XNU entry**.
XNU's first branch instruction executes. The temporary breakpoint at
its destination should then trap into our EL1 exception vectors
*before the next original XNU instruction executes*.

The EL1 handler logs ESR_EL1, ELR_EL1 and FAR_EL1, issues HVC #0x21,
and the EL2 handler returns to the existing shim. The shim restores
the original destination word and invalidates the instruction cache
again, then logs completion and returns to TWRP. This proves at most
one real XNU instruction, and does not establish a running XNU kernel.

## Expected USB CLOG events

Stage 21 is the diagnostic on-wire stage identifier for Stage20-B.
The original Stage19 segment copy events remain.

| Event | Evidence |
|---|---|
| 0x20b0 | Entry 0x8142d488, target 0x8142f0ec, B opcode 0x14000719 |
| 0x20a0 | Pre-ERET at EL2 |
| 0x20a1 | Shim EL1 pre-branch checkpoint (CurrentEL 4) |
| 0x20b2 | EL1 BRK exception: ESR EC 0x3c / ISS 0xb20; ELR 0x8142f0ec |
| 0x20b3 | HVC #0x21 at EL2: ESR EC 0x16 / ISS 0x21 |
| 0x20a3 | Return to EL2 (CurrentEL 8) |
| 0x20b4 | Target restored; all three arguments 1 |
| 0x20ff, 0x19ff | Completion with status 0 |

Failure to receive all checkpoint events is **not** evidence that XNU
executed. A fault may prevent the recovery chainload; the proven
manual fastboot recovery procedure remains available.

## Build and capture

From WSL Ubuntu in the shim directory, run:

    python3 test_stage20b_entry.py
    python3 test_cactus_usb_decode.py
    ./build_trace_probe.sh 21

Image: out/cactus/cactus-trace-stage-21-boot.img.
This is separate from the previously proven Stage20-A image.

For a future authorized hardware test, start watch-cactus-usb.ps1
before flashing. Decode the resulting COM25 session with:

    python decode-cactus-usb.py CAPTURE.bin --show 85 --require-stage20b

Verify TWRP boot afterwards. Hardware proof remains outstanding.
