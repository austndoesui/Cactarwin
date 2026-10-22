# Cactus XNU bring-up: USB logging implementation plan

Status: source-verified plan, 2026-10-22. No full XNU USB console
implemented or hardware-tested by this plan.

## Established hardware facts

- **USB transport works in the standalone shim probe.** Stage 12 with
  MT6765 PHY recovery and fixed MUSB EP0 SETUP handling enumerated
  VID_1209/PID_0001, opened Windows CDC ACM COM25, and delivered eight
  byte-exact copies of the 32-byte CLOG test packet (256 bytes).
  Known working source: `shim/musb_cdc_probe.c`; tested image:
  `shim/out/cactus/cactus-trace-stage-12-working-boot.img`.
- Stage 12 uses a **10-second, single-threaded, polling** loop. It emits
  fixed bytes only, requires the host to open the COM port and assert
  DTR, and then stops. It does **not** run XNU or retain an asynchronous
  USB worker across a kernel branch. Its attempted TWRP return was not
  observed through ADB within 44 seconds.
- The normal cactus shim in `shim/start.S` is entered at EL2 (observed
  in stage probes), and its `xnu_enter_el1` transition was proven by
  Stage 5. At initial entry it still sets VBAR_EL1 without selecting
  VBAR_EL2 first. This needs correcting before relying on exceptions.
- The XNU payload loader in `shim/loader.c` checks basic ranges but
  does not yet account for every live FDT reservation/shim/ramdisk
  occupancy. See `BENESSE-CACTUS-PORT-PLAN.md`.
- Cortex-A53 requires **4 KiB CPU translation pages**, with XNU's
  experimental **16 KiB software VM pages**, 39-bit VA, GICv3 and the
  cactus Android v2 boot format. Do not copy Benesse's 16 KiB hardware
  translation, GICv2 driver, UART base or Android v0 packer.

## Core design

Keep a single-producer **binary event ring** and a distinct
**polled CDC transmitter**. A trace write must work even when USB is
unconfigured, DTR is low, the host is unplugged, or the CPU is inside
an exception. USB transmission is best-effort and bounded.

The host receives event records while it is connected. Every record
includes version, length, boot/session ID, sequence, stage/CPU/EL,
architectural counter ticks, event ID and up to three 64-bit values;
framing uses CLOG magic plus a checksum so the decoder can resync
after a partial packet. Include a dropped-record count. Treat USB as
a byte stream, not one COM read per event. Add a separate bounded
text-chunk record format for later console output; start with numeric
checkpoints so logging itself cannot run formatters or allocations.

Memory for the ring and CDC state must be **explicitly reserved out of
the live FDT's usable DRAM**, outside the incoming payload, loaded XNU
segments, stacks, ADT, boot_args, TWRP chainload image and DTB. Do not
blindly use the old initramfs CPIO scratch address or persistent
ramoops region. Reserve enough space via the XNU boot memory boundary
or equivalent verified mechanism, document PA and usable VA in a small
versioned handoff descriptor, and prove XNU does not recycle the range.
The event ring can survive a temporarily unavailable USB link; it is
**not** guaranteed to survive reboot or a later recovery image.

### Stage 14: extract a reusable CDC transport (offline first)

**Files:** `shim/musb_cdc_probe.c` (keep as working reference), new
`shim/cactus_usb.[ch]`, `shim/cactus_trace.[ch]`, host parser and
capture scripts.

1. Extract the known-working controller/PHY, enumeration, EP0,
   SET_ADDRESS, SET_CONFIGURATION, CDC DTR and bulk-EP1 handling into
   small functions: `usb_init`, `usb_poll`, `usb_send_budgeted`,
   `usb_shutdown`. Keep the old Stage 12 file and verified image
   untouched for A/B regression checks. No heap, DMA, threads or
   interrupts. Keep all read/write and transfer waits bounded.
2. Emit distinct increasing sequence numbers, varied payloads
   including zeros/high-bit values, multiple lengths and ring
   wrap-around. Implement a Windows receiver which captures raw bytes
   to disk incrementally, decodes framing/checksums, flags missing
   sequence numbers and reports disconnect/re-enumeration. Open the
   COM port and assert DTR before testing transmission.
3. Build with local WSL cross-toolchain and the existing cactus
   Android boot v2 packer. The first test runs as another isolated
   Linux/TWRP probe, not an XNU boot.

**Gate:** host captures and validates a sequence of different event
records, including after a deliberate pause/host reopen, without
corruption or unbounded waits. Recheck the tested Stage 12 image if
enumeration regresses. Record whether TWRP returns; debug recovery
chainloading separately, not as proof of data transport.

### Stage 15: trace the real shim before XNU

**Files:** `shim/start.S`, `shim/shim.c`, `shim/loader.c`,
`shim/fdt.[ch]`, `shim/build.sh`.

1. Install the correct **VBAR_EL2 or VBAR_EL1** from CurrentEL at
   shim entry; set an EL1 vector before ERET and emit bounded
   ESR/ELR/FAR/SPSR checkpoint information for a synchronous trap.
   Preserve existing working EL2-to-EL1 setup and x0 boot_args.
2. Adapt Benesse's **bounded FDT memory/reservation validation**.
   Validate Mach-O destination, executable/file-backed entry, ADT,
   boot_args, shim and trace ring before writing target memory.
3. Add an option to start CDC once, allow a bounded host-open/DTR
   window, drain the ring at defined milestones, and **leave the
   controller configured** across handoff when the next stage can
   service it. Do not run the existing 10-second probe loop as a
   blocking call around XNU.
4. Emit `SHIM_ENTRY`, `DTB_OK`, `PAYLOAD_VALID`,
   `SEGMENTS_COPIED`, `BOOTARGS_READY`, `EL1_READY`,
   `XNU_BRANCH`, with PA/VA/EL and error codes. Keep first-CPU
   bring-up `cpus=1`.

**Gate:** on a build that deliberately stops **before** XNU, the
Windows capture proves those ordered events. A separate controlled
EL2-to-EL1 probe confirms the right exception vectors; errors emit an
explicit failure frame before stopping. No uninstrumented XNU flash.

### Stage 16: prove the first instruction of XNU executes

**Files:** new cactus-specific patch to the actual XNU
`osfmk/arm64/start.s` entry path, XNU linker/build packaging,
trace handoff descriptor.

1. Follow Benesse's proven `start_first_cpu` **kernel-owned entry
   marker** idea, but write the cactus record to the shared ring.
   The first marker must run **before stack/MMU/C initialization**
   using only scratch registers with correct preservation of the
   boot_args x0 ABI. Add additional markers around stack setup,
   TTBR/TCR/SCTLR programming, and the first C-level `arm_init`.
2. In the first controlled image, emit one XNU-owned marker and return
   through a deliberately arranged diagnostic path to the shim for
   USB draining, or use a narrowly reviewed, bounded polling transmit
   callable from the kernel at this point. Ensure the shim remains
   resident and its memory is outside the loaded kernel before
   attempting a return; otherwise use the direct transmit option.
3. Prove XNU starts with **MMU off at EL1**, confirm x0 points to
   valid boot_args, and identify the *actual* executed kernel entry,
   not merely the shim's jump log. Treat a blank LCD as inconclusive.

**Gate:** a **verified XNU-origin** record arrives at the host, with
kernel entry PA and CurrentEL. A shim-only `XNU_BRANCH` event is
insufficient.

### Stage 17: keep logging after XNU enables its MMU

**Files:** cactus XNU bootstrap page tables in `start.s`, early
platform code (`arm_init.c` and early console paths), optional
polled USB shim compiled into the XNU kernel.

1. Establish explicit **Device-nGnRnE mappings** at approved XNU KVA
   aliases for MUSB `0x11200000`, PHY `0x11cc0800` (as needed)
   and a **Normal cache-coherent mapping** for the reserved trace
   ring. Correctly maintain cache barriers before XNU ownership
   changes. Do not dereference old physical pointers after the
   identity mapping or TTBR0 is removed.
2. Keep MUSB ownership single-threaded and **polled** through early
   milestones; invoke `usb_poll/drain` from known-safe XNU hooks
   only. Do not attempt an EL2 background service or USB IRQ driver
   until bootstrap is stable. Neither entry vectors nor background
   timers make logging asynchronous for free.
3. Add records before/after MMU switch, entry to `arm_init`,
   platform/DT initialization, VM init, interrupts, timer, first
   scheduler activity and controlled panic. For exception paths,
   always write ESR_EL1, ELR_EL1, FAR_EL1 and SPSR_EL1 to the ring
   before attempting bounded USB transmit. Instrument the **XNU
   exception vectors**, because it replaces the shim's VBAR.
4. Once milestones survive the MMU transition, route a minimal
   `PE_serial`/early console putc or equivalent XNU debug console
   backend to buffered USB text records. Do not perform unlimited
   spin waits, allocate memory, or recursively call panic/logging
   APIs from this backend. If XNU reinitializes/reset the USB PHY,
   expect a fresh enumeration and a new session marker.

**Gate:** capture a post-MMU XNU-owned event, then a deliberately
triggered XNU fault or panic with reliable error registers. Only
after that claim usable early XNU runtime diagnostics.

## What can and cannot be promised

- We **can** capture real-time logs at instrumented points as long as
  the host sees CDC and the running CPU can service USB. A host-side
  incremental capture retains already-delivered bytes even if the
  phone hangs permanently.
- We **cannot** promise every printk, uninstrumented instruction,
  exception before vectors exist, or logs after a total CPU stall/
  memory corruption. A polling USB backend runs only when XNU calls
  it. Logging through every kernel state needs an XNU-owned USB
  driver/console and working VM, scheduler and interrupt support.
- If XNU fails before the host has opened COM/DTR, keep the earliest
  events in the reserved ring, and flush only if control later reaches
  another safe USB service point. A subsequent hard reset cannot be
  counted on to recover this volatile ring; ramoops/pstore remains
  an optional second transport, not a replacement for the working
  live USB cable.
- Do not assume the standalone Stage 12 probe's attempted
  automatic TWRP chainload works after USB initialization. Test and
  fix its controller shutdown/recovery handoff separately.

## First concrete deliverable and stop conditions

**Next coding task:** implement Stage 14 only, with varied framed
records, an incremental Windows capture/decoder, offline validation,
and a TWRP-return diagnostic image. Avoid touching XNU yet. When
Stage 14 passes hardware, implement Benesse-style EL/FDT preflight
and shim integration in Stage 15. These two steps expose failures
without conflating MUSB transport with XNU address-space bugs.

All device writes must be an explicit, verified cactus boot test using
the known recovery path; no flashing is part of writing this plan.
