# Stage 27 — Rebuild patched Cactus XNU and validate bootstrap mappings

**Actual outcome (2026-10-23):** macOS GitHub Actions run `39995786681`
completed successfully for `CACTUS`; the extracted ARM64 Mach-O
`kernel.development.cactus` has SHA-256
`a1b8677a3449664fd17a9ee4b3740a76d2d489d0dbf1dce5e4ca5887f3840008`.
Stage32 diagnostic image SHA-256
`baa92d94c5460dec236d61ad008fd49ab4dbaba0b6917f05380a232cfbdce18c`
was flashed and ran EL1, producing **71 valid COM25 frames**. XNU
still panicked through `pmap_map_bd.cold.1`. A follow-up Stage33
capture shows the exact remaining missing L1 at
`0xfffffff03b000000`; see [Stage28](STAGE28-FRAMEBUFFER-PLAN.md).

**Objective:** compile a new XNU Mach-O for the Redmi 6A with the actual
early-I/O page-table correction, then test native execution with the
proven COM25 diagnostic shim. This is **not** yet evidence of a boot.

## Stage 26 baseline

- Build 31 ran the **old** XNU payload with a temporary 640-row video
  handoff. The Stage 26 hardware capture was clean but XNU still
  panicked in `_pmap_map_bd.cold.1`.
- Stage 25 measured the missing L1 descriptor at VA
  `0xfffffff04a000000` while mapping framebuffer PA
  `0x5e605000..0x5e9fa000`.
- The real correction is
  `xnu/patches/0086-cactus-early-io-l1-tables.patch`, which invokes
  `init_ptpages()` across the aligned early I/O L1 range **before**
  allocating its L3 pages.

## Build recipe

Use a dedicated Git branch to isolate the XNU patch from pending local
Stage 26 shim changes. The GitHub Actions `xnu` workflow already uploads
`xnu-kernel` and `xnu-buildtree` artifacts. It must run with the
**Cactus** machine setting rather than the inherited SWEET default:

```sh
gh workflow run xnu.yml --repo austndoesui/Cactarwin \
  --ref stage27-cactus-l1-fix \
  -f macos_version=13.5 \
  -f machine_config=CACTUS \
  -f kernel_config=DEVELOPMENT \
  -f keep_going=false
```

For a successful run, download `xnu-kernel`, locate the exact Cactus
Mach-O file, verify architecture, machine config, segments and SHA-256,
and **only then** update the local shim payload. Avoid inventing
boot-image hashes or treating a successful compile as hardware proof.

## Native boot and diagnostics

The next shim should keep full-size 720x1440 video boot arguments and
preserve original XNU exception vector and COM25 register logging.
The Stage 31 reduced-height experiment is not an appropriate test of
the rebuilt allocator. Prior checkpoint and panic-address guards must
be revalidated against the **new** Mach-O: recompilation can change
function addresses, section sizes and opcode words.

Success gates: (1) kernel builds and produces a real Cactus Mach-O;
(2) updated shim passes its boot-image/payload checks; (3) physical
Redmi 6A boots that image and reaches native EL1; (4) COM25 evidence
shows execution past the old `_pmap_map_bd()` panic. Hardware scanout
and on-screen text are separate unproven goals.
