# Stage 21-B — cactus XNU MMU compatibility audit (static)

**2026-10-22: read-only binary audit completed. No boot image was built
or flashed for this stage.** The latest live evidence remains the
hardware-passing Stage20-D (47 CRC-valid frames, TWRP recovered).

## Reproducible inputs

- Inspected the real *relocated* SWEETXNU payload included in the
  Stage20-D boot chain: `shim/out/cactus/linux-chainload/stage18-payload.bin`.
  Length **16,540,608 bytes**, SHA256
  `f1cbeff59113e27a66e4ed70e4b2b59c5318aa36825ac0d961040f7ae3bb5b1f`.
- Parsed the `SWEETXNU` header, all ten Mach-O segment records and
  their file-backed PA→VA offsets. Disassembled the *installed*
  ARM64 instructions with `aarch64-linux-gnu-objdump`.
- Reproduction, from `ios-cactus/shim`:

  ```sh
  python3 check_stage21b_mmu.py out/cactus/linux-chainload/stage18-payload.bin
  python3 -m unittest -v test_stage21b_mmu
  ```

  The audit pins every relevant system-register instruction plus an
  SHA256 of the entire 828-byte MMU setup code block
  `[0x8142f154,0x8142f490)`. Changes to this payload fail closed.
  All statements below about what the **CPU actually executed** past
  Stage20-D remain predictions until a hardware capture validates them.

## Translation parameters decoded from actual XNU instructions

| Finding | Evidence / value |
| --- | --- |
| Translation granule | **4 KiB** for both TTBR0 and TTBR1. `TCR_EL1.TG0=0`, `TG1=2`. |
| Address-space widths | `TCR_EL1 = 0x22a5192519` stored in payload at PA `0x81178a18`. `T0SZ=T1SZ=25` (39-bit VA regions), `IPS=2` (40-bit PA). `EPD0=EPD1=0`. |
| Table geometry | 4 KiB tables; 512 64-bit descriptors per table; 2 MiB level-2 blocks. The startup uses `>>21`, `+0x1000`, and `0x601` block descriptors. |
| Table-pool allocation | **`[0x8117a000,0x8117e000)`**, four initially zeroed 4 KiB pages inside the loaded kernel segment. |
| TTBR0 | **`0x8117a000`**; actual `msr ttbr0_el1,x0` in helper at **`0x81433f68`**, called at `0x8142f3d8`. |
| TTBR1 | **`0x8117b000`**; `msr ttbr1_el1,x0` at **`0x8142f3e8`**. The startup `x21=0` path skips the alternate TTBR1 override at `0x8142f460`. |
| Memory attributes | `MAIR_EL1 = 0x0c08044f00bb44ff`; installed at **`0x8142f430`**. Attr0=0xff (WB), attr1=0x44 (Normal-NC), other indices as encoded. |
| MMU enable | `SCTLR_EL1 = 0x3454593d`, with **M/C/I enabled**, written at **`0x8142f488`** followed by **ISB at `0x8142f48c`**. First next instruction at `0x8142f490` (virtual counterpart `0xfffffff00742f490`). |
| XNU software VM page | **16 KiB**, per repository's `0084-cactus-16k-pte-align.patch`, `0085-cactus-commpage-16k-align.patch`, and prior cactus notes; this is distinct from the 4 KiB hardware translation granule confirmed in the binary. |

**Important source mismatch:** `shim/mmu.c` still contains a separate,
older *16 KiB hardware* translation-table implementation
(`PG_SHIFT=14`, `TCR.TG0=16K`). Cactus `shim/board.h`
specifies `BOARD_MMU_16K=0` for Cortex-A53. The successful
Stage20-D entry path bypasses that old shim helper and runs XNU
with the MMU off; Stage21-C must follow the **actual 4 KiB XNU**
bootstrap, never substitute `shim/mmu_enable()`.

## Exact bootstrap sequence and memory addressing

At `0x8142f148` XNU reads `boot_args` (PA `0x824bc000`)
into `x22=virtBase`, `x23=physBase`, `x24=memSize`.
The hardware-proven values are:

- Kernel virtual base `0xfffffff006000000`.
- Managed physical RAM `[0x80000000, 0x9e000000)` (480 MiB).
- Loaded kernel Mach-O starts at PA `0x81004000`.
- Apple device tree PA `0x824b8000`, boot_args PA `0x824bc000`.

Startup subtracts the `0x01004000` prefix, yielding
`x22=0xfffffff007004000`,
`x23=0x81004000`, `x24=0x1cffc000`.
It builds a physical TTBR0 bootstrap map and corresponding high
virtual TTBR1 map from the 4 KiB table pool.

The early kernel stack is set to **virtual
`0xfffffff0080ac000`** (PA `0x820ac000`) *before*
MMU activation, without dereferencing that stack until the
translation switch. The kernel text, stack, XNU vector pages,
boot_args and ADT all lie in the mapped bootstrap RAM span.

**Possible last-2-MiB mapping gap:** the disassembled code calculates
the 2 MiB block count with `x24 >> 21` (floor). With this exact
480 MiB admission and the Mach-O offset, that gives 231 blocks
starting at aligned PA `0x81000000` and ending at **`0x9de00000`**.
The ADT still advertises RAM through **`0x9e000000`**. It appears
the last **`0x200000` bytes** are outside the initial bootstrap
mapping. This is a **static mapping concern**, not a proven runtime
crash or confirmation that XNU will actually access the final 2 MiB.
Stage21-C should record descriptors and test translations before
changing the ROM's RAM bounds or startup instructions.

### Framebuffer alias

XNU builds two 2 MiB, Normal-NC, execute-never blocks mapping
physical `[0x5e600000,0x5ea00000)` into the high-VA region starting
`0xffffffe000200000`. The visible framebuffer uses
physical `[0x5e605000,0x5e9f9800)`, so its **entire
720×1440×4-byte pixel area fits** into those two blocks. The
corresponding first visible pixel VA is
`0xffffffe000205000`. The rest of the firmware's reserved
`0x17bb000`-byte framebuffer region is not proven mapped by
these two entries. This proves the code's *intended* alias; working
LCD scanout and later XNU framebuffer drivers remain unverified.

The shim's USB logger runs through EL2 with its own physical MMIO
path. EL1 code cannot assume the same peripheral addresses are
accessible after XNU switches to virtual mappings.

## Exception-vector and execution-mode hazard

Stage20-D intentionally trapped **before XNU changed stacks**:
EL1h's synchronous slot was PA `0x8142e200`.
Immediately afterward, XNU executes `msr spsel,#1` at
`0x8142f174`, then **`msr spsel,#0` at
`0x8142f18c`**. By MMU setup it uses **EL1t** (SP_EL0).
Its synchronous exception uses **VBAR + 0x000**, *not* the
EL1h slot at +0x200.

After TCR, TTBRs, MAIR and TLB maintenance, XNU writes **virtual**
`VBAR_EL1=0xfffffff007429000` at PA **`0x8142f474`**.
It then enables the MMU at PA `0x8142f488` (ISB
`0x8142f48c`). **An exception in that small MMU-off interval
would try to vector through an unusable virtual VBAR address.**
Do not place a normal BRK there.

After MMU-on, the EL1t sync vector's physical backing is
**`0x81429000`**, whose original first instruction is
`b 0x8142a000` (`0x14000400`). The EL1h counterpart
`0x81429200` starts `0x140003f6`; it is the wrong slot
for this phase. A post-MMU exception hook must operate in the
**new, mapped XNU vector page** and hand off to the EL2 USB logger
without calling arbitrary physical-address EL1 code.

The `x21=0` startup path constructs its virtual return address
`0xfffffff007ac05dc`, whose backing is PA `0x81ac05dc`;
disassembly confirms a genuine C-like function prologue with stack
stores there. The first C routine's symbol name has not been verified
from a matching symbol map.

## Stage 21-C test design from these findings

1. **Pre-MMU checkpoint:** with code and tables loaded, run as far as
   PA `0x8142f474` (just *before* XNU installs the virtual VBAR),
   while the active VBAR is still physical `0x8142e000`.
   Instrument the **EL1t** synchronous slot at `0x8142e000`
   and capture `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`,
   `MAIR_EL1`, `SCTLR_EL1`, the new page-table root descriptors
   and translation of the last 2 MiB of advertised RAM. Verify clean
   HVC → EL2 → TWRP recovery before advancing.
2. **MMU-on checkpoint:** separately test the real writes to virtual
   VBAR, `SCTLR_EL1` and `ISB`, with a post-MMU BRK at
   `0x8142f490`. Its exception must use the **EL1t slot at
   physical `0x81429000` mapped to high VA**. Route directly
   to a small `HVC` stub in XNU-mapped code, so EL2 can log
   ESR/ELR/TCR/SCTLR without depending on the EL1 USB/physical
   shim mapping. Validate complete restoration and TWRP fallback.
3. **C entry:** only after MMU-on logging works, validate the
   `0xfffffff007ac05dc` return target and initial stack access,
   then approach platform expert/interrupt and display bring-up.

Stage20-D's successful SHA256-pinned stage-23 boot image remains
the hardware fallback. **Stage21-B was source and binary analysis
only; none of Stage21-C's runtime conditions have been tested.**
