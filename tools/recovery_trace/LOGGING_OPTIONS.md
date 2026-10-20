# Redmi 6A black-screen logging: evidence and next experiment

## Verified in this workspace

- The last `ENTRY_PROBE=1` build loops after a framebuffer write **before** `dbg_init()`. It cannot emit `CDB1` event records.
- Standard shim calls `dbg_init()` only in `shim_main()`, after `_start` has written `VBAR_EL1`, switched stack, and cleared BSS. Any failure before that point is invisible to its event recorder.
- `SHIM_EARLY_PSTORE=1` writes only a magic and version near `_start`. It does not save the current exception level, fault registers or a sequence of milestones; its default is off. Its DRAM target `0x47c90000` overlaps the recovery ramoops region.
- The normal `mark()` path writes ring entries at `0x47c91000`, after the trace is initialized, but a crash before initialization cannot reach those marks. An exception handler can display text, yet the panel may be inactive (`atag,videolfb-islcm_inited=0`).
- Existing recovery kernel is 4.9.190+, with `CONFIG_MODULES=y` and `CONFIG_MODVERSIONS=y`; no ABI-compatible built module has been produced. `/dev/mem` in the tested TWRP gave ENXIO; pstore did not expose the custom trace.

## Ranked options

1. **External UART capture** is the most robust channel once the correct physical debug pins, voltage, mux, clock and baud rate are established. The DTS indicates candidate UART controller addresses `0x11002000` and `0x11003000`, not verified accessible external pins or an initialized transmitter. Do not connect a 5 V adapter or write registers speculatively.
2. **Early trace in dedicated reserved DRAM plus recovery-kernel snapshot before ramoops** is viable in principle. It needs a non-overlapping region verified against the *live* memory map for both bootloader and recovery, early retention validation, and a custom recovery build. A normal loadable module initialized after ramoops is insufficiently reliable.
3. **Bootloader-exposed crash logging / SRAM / watchdog reason** could help identify a reset, but no readable retained early-shim channel has been demonstrated. Examine LK source and recovery-exposed reset-status nodes before relying on these.
4. **Framebuffer markers** are weak evidence because panel initialization is not guaranteed. `adb logcat` in TWRP cannot run concurrently with bare-metal shim. Fastboot USB disconnect/reconnect timing is only a coarse sign of reset or entry, not a code trace.

## Recommended gate before next shim flash

First establish one independent observable signal: confirm UART traffic from a known-good Android/TWRP boot **or** build a separate recovery kernel that snapshots the intended non-overlapping trace region before other drivers initialize. Then run a small known-stage diagnostic that records entry, current EL, DTB pointer, stack/BSS completion and exception `ESR/ELR/FAR` without accessing unknown peripherals. Validate decoding on the host and verify retention through a controlled warm reboot. Only then use the channel on the XNU-shim black-screen case.

Do not infer failure location from a black display. Do not flash the current reader module source or the original `recovery.img` as a logging solution: no recovery-side integration has been validated.
