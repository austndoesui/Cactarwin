# Cactus TWRP-return trace experiment

The Redmi 6A reaches TWRP from the existing Linux stage probes, but the
full XNU image hangs on a black screen. The old `CDB1` trace in ramoops
cannot be read in stock TWRP. This experiment carries a diagnostic record
through an **additional raw CPIO archive** appended to the original recovery
gzip ramdisk. The original TWRP kernel and gzip ramdisk bytes are unchanged.
The 4.9 initramfs parser supports adjacent gzip and raw CPIO archives.

The new raw CPIO file is `/cactus-trace.bin`. Before Linux starts, the probe
writes into its data buffer at the fixed cactus ramdisk address. TWRP's
initramfs extractor should then publish those bytes as an ordinary file.
There is no physical storage write, custom recovery-kernel build, UART,
USB gadget implementation, RTC write, or watchdog programming.

Three stage probes:

- **Stage 3:** emit a live `CTOK` record with DTB, EL, MPIDR, timer rate,
  watchdog MODE/STATUS and hand off to original TWRP. This first tests
  whether the extra initramfs file reaches recovery.
- **Stage 4:** run the real `shim/debug.c::dbg_init()` (which writes the
  previously tested ramoops memory region), copy 20 KiB of `CDB1` trace
  into that file, emit `CTOK`, and hand off to original TWRP.
- **Stage 5:** from the measured EL2 bootloader entry, set up EL1 and
  `eret`, record the new `CurrentEL` into `CTOK`, and boot original TWRP.
  The expected trace has `stage=5` and `CurrentEL=0x4`. This checks the
  entry-level transition independently of the XNU payload.

To rebuild **offline** in WSL from `ios-cactus/shim`:

```sh
bash build_trace_probe.sh 3
bash build_trace_probe.sh 4
bash build_trace_probe.sh 5
```

The output images are `out/cactus/cactus-trace-stage-{3,4,5}-boot.img`. They use the existing
`0x40080000` kernel, `0x51b00000` ramdisk and v2/2048-byte layout.
Do not flash during the user's ALPS recovery. Inspect the hash of each
image and verify fastboot reports cactus before any later **authorized**
boot partition flash. Keep the original ALPS boot and TWRP recovery
available for rollback.

After an approved stage-3 test, if recovery is detected by ADB:

```powershell
adb devices -l
adb pull /cactus-trace.bin "F:\ROM\patching\redmi 6a xnu\cactus-trace.bin"
python "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\trace_initramfs.py" decode "F:\ROM\patching\redmi 6a xnu\cactus-trace.bin"
```

Expected first-stage evidence: the exact 20,544-byte file exists and its
magic is `CTOK`, `stage=3`, `status=1`. For stage 4, the same file
should show `stage=4` and an embedded `CDB1` header. Optionally decode
the original reserved-RAM trace format using:

```powershell
python "F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\trace_initramfs.py" decode "F:\ROM\patching\redmi 6a xnu\cactus-trace.bin" --extract-cdb "F:\ROM\patching\redmi 6a xnu\cactus-cdb.bin"
python "F:\ROM\patching\redmi 6a xnu\ios-cactus\tools\cactus-pstore.py" "F:\ROM\patching\redmi 6a xnu\cactus-cdb.bin"
```

If recovery boots but the file is absent, investigate the actual TWRP
initramfs extraction and its root filesystem. If it exists but still says
`CTRC`, Linux unpacked the file but the probe did not finish writing:
the bootloader may have placed the ramdisk elsewhere. Both cases
remain inconclusive about XNU entry.

**Limitation:** a hung XNU image does not hand off to Linux, so the file
cannot be extracted on that same boot. The technique yields concrete
evidence for **recoverable staged probes**. Reliable post-hang capture
still requires a proven watchdog/recovery handoff with retained evidence,
or a verified pre-ramoops reader in a recovery kernel, or a separately
allocated persistent medium. None is yet demonstrated on this device.
