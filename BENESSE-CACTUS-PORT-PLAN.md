# Redmi 6A cactus: adapting the Benesse MTK XNU port

Source review: 2026-10-22. This is a source-based implementation plan,
not a claim that a new XNU build has booted on cactus.

## What the other port actually establishes

The locally cloned Benesse sources are three sibling repositories inside
`../xnu_device_benesse_a04br3/`:

- `xnu_bootshim_benesse_a04br3/` — LK entry, EL2-to-EL1 transition,
  bounded FDT and Mach-O payload parsing, XNU ADT/boot_args, UART reports.
- `xnu_device_benesse_a04br3/` — MT8167 DTS, board configuration,
  Android/MediaTek boot image, payload packer, host tests.
- `xnu_kernel_benesse_a04br3/` — XNU board patches and peripheral drivers.

The Benesse *device* README records a **2026-10-20 hardware milestone**:
kernel `89fea0f`, bootshim `556aa15`, device `41dc5bc` mounted a
generated HFS+ RAMDisk and ran a diagnostic PID 1 reading
`/etc/a04br3-stage`. This does **not** establish boot of an ordinary
iOS/macOS desktop. The header in the bootshim README predates that
milestone and still describes the earlier watchdog-reset state. The
cloned `sources.lock.json` tracks newer revisions; individual claims
need to be matched to the commit and observed hardware log.

## Do not interchange the board-specific pieces

| Component | Benesse MT8167 | Cactus MT6761/MT6765 |
| --- | --- | --- |
| CPU | 4 x Cortex-A35 | Cortex-A53 family; confirm live topology |
| Hardware translation granule | 16 KiB in A04BR3 XNU config | **4 KiB**; XNU software VM pages 16 KiB |
| GIC | **GICv2**, MMIO CPU interface | **GICv3**, GICD 0x0c000000, GICR 0x0c100000 |
| LK entry | EL2/EL1 permitted, checked dynamically | Stage probe observed **EL2** |
| Logging | inherited MTK UART 0x11005000 | no UART adapter; MUSB CDC still lacks validated bulk transfer |
| Android image | header **v0** plus 512-byte MTK wrappers | header **v2**, original appended DTB and separate DTBO |
| Loader payload | `A04XNU01` v1–v6, generated ADT | `SWEETXNU` v1, prebuilt ADT |
| XNU RAM | Benesse-managed window 0x60000000..0x78000000 | must be selected from **cactus live FDT** with reservations |
| Root device | Benesse MSDC + HFS development root | cactus eMMC/MSDC hardware mapping/driver still needed |

Do not import `A04BR3.h`'s `__ARM_16K_PG__` or its GICv2
`pe_a04br3_gic.c`. Do not run Benesse `tools/bootimg.py` against a
cactus image, use Benesse's TRNG register offsets, or flash its GPT.
Retain cactus's Android v2 packager and the current recoverable TWRP test path.

## Observed cactus baseline

- Stage 5 verified EL2 -> EL1 with `CurrentEL=0x4` and TWRP return.
- Stages 7/9/10/11 passed limited MUSB diagnostics. Stage 13 proved
  that MT6765 PHY device-mode initialization produces USB host traffic.
- **Update 2026-10-22:** Stage 12 **proved fixed-frame USB bulk transfer**.
  Windows enumerated CDC COM25 and captured eight identical 32-byte
  CLOG packets (256 bytes) after fixing EP0's STATUS_OUT/new-SETUP path.
  This does not establish an XNU-side logger or reliable recovery
  chainloading. See `XNU-USB-LOGGING-PLAN.md` for the next staged tests.
- Earlier full `shim-xnu-el1-boot.img` was flashed, but no XNU-owned
  execution marker or recovery trace was captured. A blank display is
  not an XNU instruction trace; LK may leave the LCD off.

## Implementation sequence and acceptance gates

### 1. Make the earliest cactus shim EL-aware

**Files:** `shim/start.S`, `shim/shim.c`, optionally the isolated
`shim/linux_stage_probe.S` test harness.

Benesse `start.S` reads `CurrentEL` and installs `VBAR_EL2` or
`VBAR_EL1` before executing C or making an EL transition. Cactus
`shim/start.S` currently writes **only** `VBAR_EL1` at `_start`,
although the real LK entry is EL2. Move the EL dispatch and matching
exception vectors to the first-stage entry. Keep the existing,
hardware-tested `xnu_enter_el1` transition; deduplicate after the
safe entry vectors are working.

**Gate:** a standalone boot probe records the expected entry EL,
vector ownership and EL1 transition, and still returns to TWRP. A
probe's success does not establish XNU kernel entry.

### 2. Make the live FDT authoritative for RAM placement

**Files:** `shim/fdt.[ch]`, `shim/loader.c`, host-only tests.

Port the **bounded parsing and validation concepts** from Benesse
`fdt.c` and `handoff.c`: DTB size/version/offsets, address/size
cells, the FDT memory-reservation map, `/reserved-memory`, DRAM
windows, bootloader initrd, FDT bytes, shim image and stacks.
Check every destination segment, zero-fill tail, Apple DT, boot_args,
entry point, and original payload against those ranges **before any
kernel copy**. Check all address arithmetic for overflow.

Cactus currently checks the SWEETXNU payload and ADT structure but
does not enforce the whole live reservation inventory. The source
build's preselected `phys_base` is not itself proof that RAM is free.
Keep the `SWEETXNU` wire format until a deliberate converter exists;
Benesse `A04XNU01` headers are incompatible.

**Gate:** offline tests using cactus DTB plus synthetic malformed and
overlapping ranges reject every unsafe placement; an accepted layout
fits only known free RAM. No physical overwrite in negative tests.

### 3. Audit the XNU entry ABI, virtual addresses and page tables

**Files:** `shim/loader.c`, `shim/start.S`,
`xnu/patches/0082-cactus-machine-config.patch`, later XNU patches.

Both loaders use Revision/Version 2 Apple `boot_args` (736 bytes),
and the Apple DT pointer must refer to the right **virtual** address.
Check each field against the actual cactus-built XNU `boot.h` and
its Mach-O VM regions, as well as a canonical 39-bit TTBR1 virtual
window. Confirm entry is in a *file-backed executable* segment;
reject overlapping Mach-O segments. Preserve the physical `x0 =
boot_args` on EL1h handoff, MMU/cache off with appropriate barriers.

Cortex-A53 does not provide the 16 KiB hardware translation granule
selected by Benesse. Keep cactus's 4 KiB translation plus 16 KiB XNU
software-VM configuration, including existing `0084/0085` alignment
fixes. Audit `shim/mmu.c` separately: it contains SWEET-era 16 KiB
page-table assumptions and must not silently be enabled on cactus.

**Gate:** inspect the produced Mach-O and payload offline: load
segments, entry, virtual base, boot_args, ADT and all page-granule
settings agree. Build the tiny first-instruction/exception checkpoint
before attempting full XNU.

### 4. Produce a trustworthy XNU-owned entry signal

**Files:** cactus XNU `start.s` patch and diagnostic image build.

Benesse proves kernel execution with its **kernel-owned**
`XNU_A04BR3_KERNEL_ENTRY` UART marker after `XNU_HANDOFF_JUMP`.
Cactus must similarly distinguish shim completion from an XNU
instruction actually running. A same-boot return-to-recovery probe
can prove shim exception handling, but data in TWRP after flashing
another image will not automatically preserve a failed XNU trace.
Windows has now enumerated the isolated Stage 12 shim and received
exact binary frames. A kernel-side polling service and post-MMU
MMIO mapping remain necessary for an XNU live console.

**Gate:** one externally retrievable, XNU-origin marker or exception
record, with its physical/virtual execution address and ESR/ELR/FAR
if a fault occurs. If unavailable, report XNU entry as unconfirmed.

### 5. Bring up only the first CPU and essential platform code

**Files:** cactus XNU machine config, `osfmk/arm64/start.s`,
platform expert, interrupt and timer paths.

Use Benesse's order, but retain **GICv3**, cactus physical addresses,
13 MHz architectural timer (virtual timer INTID 27) and Cortex-A53
feature set. Start with `cpus=1`, page tables, interrupt routing and
basic VM allocations; enable SMP only after actual MPIDR/core topology
has been verified from the cactus DTB.

**Gate:** confirmed XNU-owned early markers survive MMU enable;
timer interrupts and a controlled kernel panic can be identified.

### 6. Root filesystem and device support only after kernel progress

Benesse's source-built RAMDisk plus diagnostic PID 1 is the right
initial userspace model. Port its generic payload/root approach only
after cactus has reliable XNU entry and VM/timers. An eMMC-root driver
needs MT6765 MSDC clock, pin, interrupt and device-specific register
work, and should begin with **read-only** I/O. Framebuffer, touch,
WLAN and ordinary iOS/macOS userspace follow separate acceptance gates.

## Shortest next patch

Implement **step 1** in `shim/start.S` with an EL-specific vector
selection and a small same-boot recovery-return test. In parallel,
prepare **step 2** as a host-tested FDT/reservation validator. Neither
requires a full Linux 4.19 kernel rebuild or GitHub Actions. Once
those pass, run one XNU entry test designed to identify its **first
observable XNU instruction**, rather than another uninstrumented
black-screen flash.

Nothing in this review authorizes declaring the Benesse board image,
boot shim, page table setup or TRNG code compatible with cactus.
