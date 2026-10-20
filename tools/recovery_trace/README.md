# Cactus recovery trace snapshot (experimental)

## Built-in capture before ramoops (preferred path)

`cactus_early_snapshot.c` is a separate **built-in** implementation using an
`early_initcall` to copy 20 KiB of physical trace RAM before ramoops's
`postcore_initcall` driver registration. It exposes the cached result as
`/proc/cactus_trace` at a later initcall. The copy is read-only and does not
depend on the `/dev/mem` device or on inserting a late module. It is only a
candidate: `memremap` availability, retained DRAM and the actual initialization
order all require validation on a rebuilt recovery kernel.

After obtaining the **matching** Linux 4.9.190+ source/config for your TWRP,
install into an *isolated working copy* with:

`python3 install_early_snapshot.py /path/to/matching/recovery-kernel`

The installer adds a built-in object in `fs/pstore/Makefile`. Configure and
compile the recovery kernel normally, reconstruct a new Android boot v1
recovery image with the original ramdisk and board DTB, then verify its layout
and boot *before* attempting a shim black-screen test. Never overwrite the
original `recovery.img` during this process.

The locally available Xiaomi source under `/home/lapdq/cactus-4.9` is
**4.9.77**, whereas this TWRP binary reports **4.9.190+**. It has not been
established that the former can produce a compatible recovery kernel. No
bootable modified recovery was generated from this mismatch.

`cactus_trace.c` is a read-only kernel module for **a matching TWRP 4.9.190+ kernel**. On insertion, it copies 20 KiB starting at physical `0x47c90000` into kernel heap once, then exposes the saved bytes at `/proc/cactus_trace`. The trace header and 256 × 56-byte records fit within the snapshot. It never writes to the reserved DRAM region or flash.

**This is source only, not a tested recovery image or usable `.ko`.** The supplied recovery image identifies as Linux `4.9.190+`, built by `dinolek` on 2021-08-15. Its embedded `IKCONFIG` can be extracted with `scripts/extract-ikconfig`; it enables `CONFIG_MODULES=y` and `CONFIG_MODVERSIONS=y`. `/home/lapdq/cactus-4.9` currently lacks `.config` and generated ABI headers. Modules require an **exactly compatible** kernel source, configuration, toolchain ABI, symbol versions (`Module.symvers`), and `vermagic`; building against a similar 4.9 tree does not establish compatibility. Never load an unverified module on the phone.

With a verified matching kernel build directory, build using `make KDIR=/path/to/matching/kernel ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-`. Test against the recovery image and its kernel version/config before loading. In a controlled recovery test, load via `adb push cactus_trace.ko /tmp/` and `adb shell insmod /tmp/cactus_trace.ko`; then `adb exec-out cat /proc/cactus_trace > cactus-trace.bin` and decode with `python3 tools/cactus-pstore.py cactus-trace.bin`. `rmmod cactus_trace` cleans up the proc entry.

**Retention is unproven.** TWRP's ramoops driver claims `0x47c90000..0x47d70000`, so it could overwrite the custom `CDB1` header before a module ever runs. A missing header does not prove shim failed to execute. Verify ramoops probe timing and retained contents; ultimately a kernel-integrated snapshot before ramoops registration, or a separate verified reserved trace region, may be necessary. Warm reset must preserve the DRAM bank; power cycling may not.

## What a black-screen test can actually capture

This is a **shim stage/event trace**, not continuous video, kernel `dmesg`, Android `logcat`, or a complete CPU instruction history. The `ENTRY_PROBE=1` image does **not** call `dbg_init`, and `RESET_ONLY=2` also resets before tracing. Both therefore leave no `CDB1` trace even if the bootloader entered the shim. Other builds begin recording only after `shim_main` reaches `dbg_init`; a fault in `_start` or before that call cannot be diagnosed by the current format.

Flashing a restored Android boot image with fastboot does not intentionally erase the trace in DRAM, but the fastboot handoff, reset type, phone power state, and recovery's own initialization may change it. A forced shutdown, battery disconnection, or SP Flash recovery is especially likely to lose DRAM contents. Likewise, going directly from a black screen to recovery does **not** guarantee that traces survive or can be read. **Do not attempt a repeat flash based on this module alone.** First establish a working pre-ramoops capture in a separate, validated recovery build and verify it with a harmless known marker / reset test.
