# Repository guidance: Redmi 6A / cactus XNU

This repository is **Cactus (MediaTek MT6761/MT6765)**. Read [README.md](README.md), [NOTES.md](NOTES.md), and the relevant `shim/TRACE-STAGE*.md` before changing boot code. For older experiments, see [docs/CACTUS-BRINGUP-HISTORY.md](docs/CACTUS-BRINGUP-HISTORY.md). `../issue.txt` is an older handoff; current hardware evidence in `NOTES.md` takes precedence.

## Verified hardware constraints

- Stock LK loads Android **boot image header v2**; the diagnostic shim can call into embedded XNU via EL2 → EL1.
- Cactus uses **4 KiB hardware translation pages**, while the experimental XNU port uses **16 KiB software VM pages**. Do not import another device's memory assumptions.
- Raw binary diagnostics are transmitted over MUSB CDC, USB VID 1209 / PID 0001 (Windows COM port varies). Keep the event stream and strict host decoder together.
- The Stage 25/build 30 capture proves a zero L1 descriptor at VA `0xfffffff04a000000` while mapping the framebuffer. The next XNU source fix should correctly reserve/allocate early pmap tables in `arm_vm_init.c`. No Stage 26 image has been validated yet.
- LK leaves the MI logo visible during logging. Memory readback of a modified framebuffer does **not** prove visible scanout or a functioning XNU console.

## Working conventions

- Use host-side, locally reproducible tests and WSL builds for diagnostic shims. The XNU CI configuration must explicitly select `CACTUS`; the inherited workflow's default can still be `SWEET`.
- Before a hardware trial: verify `fastboot getvar product` is `cactus`, image header and hash, and start the independent COM25 watcher. The user maintains their own existing boot/recovery rollback images; **do not redo backups**.
- `fastboot boot IMAGE` has not reliably loaded the supplied temporary image on cactus. Actual authorized trials flash the boot partition. `fastboot oem reboot-recovery` can enter TWRP from fastboot.
- Preserve unrelated changes and ignored device artifacts. Never commit generated `shim/out/` content, hardware dumps, serial numbers, device calibration or private account information.
- Record a test as **PASS** only with hardware or local verification appropriate to that claim. Distinguish compiled, flashed, logged, and fully booted states. Document the next blocker and its primary evidence.
- The shim/XNU source inherited numbered patch names and some shared tooling from an older project. Do not rename, remove or indiscriminately apply those code patches solely because filenames contain `sweet`. They must be audited for Cactus hardware assumptions.
- Keep [NOTES.md](NOTES.md) brief and current; put extensive stage experiments into `shim/TRACE-STAGE*.md` or the history archive. Update these documents when a milestone changes.

## Current next task

Stage 26: fix actual XNU early page-table allocation for framebuffer `io_map()` using the observed Stage 25 VA/PA range; rebuild the XNU payload and diagnostic shim, retain COM25 first-exception telemetry, test that the previous `pmap_map_bd` panic is passed, then independently validate visible scanout.
