# Cactus XNU persistent pmsg trace

The Stage 6 device test proved that the early shim's physical pmsg writer
can leave a marker in `/sys/fs/pstore/pmsg-ramoops-0` when TWRP starts.
This diagnostic full-XNU image includes that same writer throughout shim
initialization, payload loading and the EL2-to-EL1 handoff. It includes
the existing SWEETXNU payload and is not a recovery chainloader.

Build locally from `ios-cactus/shim` in Ubuntu WSL:

```sh
BOARD=cactus TRACE_PMSG=1 TRACE_WDT=0 TAG=xnu-pmsg bash build.sh out/cactus/payload.bin
```

Output: `out/cactus/shim-xnu-pmsg-boot.img`. This is a boot partition image.
The default trace build disables the watchdog just before the XNU handoff;
it does not reset a hung device automatically. `EARLY_PSTORE=1` is forbidden
in pmsg builds because its older CDB1 marker writes into the ramoops console
zone. Recovery, kernel 4.9 and the XNU payload are not rebuilt.

Logged breadcrumbs include:

- `shim start`: shim BSS cleared; CurrentEL and DTB address captured.
- `ENTRY`, `before wdt`, `after wdt`, `entry`: shim initialization.
- `loader enter`, `loader dtb`, `loader initrd`, `loader payload`:
  DT and SWEETXNU payload recognition.
- `loader copy`, `seg copy`, `loader adt`: segment/ADT work.
- `xnu jump`, `xnu handoff`: loader completed and has an XNU entry/args.
- `xnu el1 reached`: the CPU reached an EL1 shim checkpoint after
  `eret` (or from EL1) immediately before branching to XNU. This marker
  **does not** prove that XNU executed its first instruction.

After a separately authorized flash/test, if the screen hangs, use the
existing known-good boot image to recover and enter TWRP directly.
Retrieve this file **before another reboot**:

```powershell
adb devices -l
adb -s 3184a8a17d2b pull /sys/fs/pstore/pmsg-ramoops-0 "F:\ROM\patching\redmi 6a xnu\cactus-pmsg-xnu.bin"
python -c "import pathlib,re; p=pathlib.Path(r'F:\ROM\patching\redmi 6a xnu\cactus-pmsg-xnu.bin'); data=p.read_bytes(); [print(m.group().decode('ascii','replace')) for m in re.finditer(rb'CACTUS/SHIM[^\r\n]{0,128}',data)]"
```

Native pmsg is RAM-backed. Stage 6 proved shim-to-TWRP transport, but the
marker **may disappear** across the forced reset needed to escape a hung
XNU image, a true power-off, bootloader RAM clearing, or a recovery boot
that reuses the region. An empty trace after manual rollback does not
prove the shim never ran.
