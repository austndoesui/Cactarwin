# Stage 17: live FDT memory map first

**2026-10-22: offline built and verified. No device flash/test yet.**

## Reason for this probe

The corrected USB watcher recorded five consecutive Stage16 boots at
`../usb-captures/stage-cactus-20261022-135847-596`. Every session
contained exactly 1,176 bytes / 21 good CLOG frames, ending immediately
after the successful FDT parse (`0x1612`) and the next heartbeat.
Windows COM25 subsequently disappeared and re-enumerated at roughly
27-second intervals. This repeatability makes an isolated missed host
command unlikely, but it does not distinguish a CPU fault, a firmware
reset, or loss of USB transmission.

Stage16 waits 7 seconds for USB, then 800 ms before checking the header,
700 ms before parsing FDT, and 650 ms after the successful parse.
Stage17 removes these delays **before the memory-map records** to
capture the live banks and reserved ranges immediately. The 32-frame
ring buffers this short burst until the host opens COM25 and asserts
DTR. Stage17 keeps Stage14's proven Linux/TWRP recovery wrapper and
bare-metal MUSB CDC driver. The `fdt_info` temporary is stored on
the stack instead of static BSS, to separate BSS lifetime questions.
Stage17 then polls USB for 15 seconds with changing heartbeats,
emits DONE, drains 2 more seconds, and requests the normal TWRP
chainload. No XNU is copied or executed; no `dbg_init`, LK changes,
watchdog manipulation, or writes to memory-map regions are included.

The native ARM64 function names `stage17_probe`, `stage17_fault`,
`fdt_check`, `fdt_inspect` and `fdt_region_free` were linked and
verified. Stage17 is an isolated diagnostic image, leaving the
hardware-tested Stage16 boot image untouched.

## CLOG events (stage field = 17)

| Code | Interpretation |
| --- | --- |
| `0x1700` | Entry: DTB pointer, CPU EL, counter frequency |
| `0x1701` | USB initialized: success, controller HWVERS, power |
| `0x1710` | About to read FDT header |
| `0x1711` | FDT header validation result, bytes, DTB pointer |
| `0x1712` | FDT parse result, bank count, reserved count |
| `0x1713` | About to read a specific RAM bank: index, count |
| `0x1714` | RAM bank: index, base, size |
| `0x1715` | Reserved range: index, base, size |
| `0x1716` | Initrd start/end |
| `0x1717` | XNU RAM-window verdict, base 0x70000000, length 0x50000000 |
| `0x1718` | Memory-map reporting finished, queued records, dropped records |
| `0x1702` | Heartbeat counter, USB DTR, dropped/pending records |
| `0x17ee` | CPU exception ESR, ELR, FAR if report can be sent |
| `0x17ff` | Probe complete, heartbeat count, dropped records |

## Image and test commands

From WSL Ubuntu:

```bash
cd '/mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim'
./build_trace_probe.sh 17
```

Boot-v2 image:
`shim/out/cactus/cactus-trace-stage-17-boot.img`

- Size: **28,958,720 bytes**
- SHA256: `bec9d3848b29a80edbd51727d64c40d5767cb0b068f2f609cceaa2d4af9a93d1`
- Local ARM64 build and Android boot-v2 validator: PASS.
- FDT host tests and six CLOG decoder tests: PASS.

After the device is returned to **fastboot** and its serial/product
have been verified, start the Windows receiver first:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\watch-cactus-usb.ps1' -Seconds 120
```

Then, in another terminal, flash the Stage17 diagnostic **once**:

```powershell
fastboot -s 3184a8a17d2b getvar product
fastboot -s 3184a8a17d2b flash boot 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\out\cactus\cactus-trace-stage-17-boot.img'
fastboot -s 3184a8a17d2b reboot
```

The receiver saves `session-001-*.bin`, `session-002-*.bin`,
etc. to a new timestamped folder. To check a session explicitly:

```powershell
python 'F:\ROM\patching\redmi 6a xnu\ios-cactus\shim\decode-cactus-usb.py' 'PATH_TO_SESSION.bin' --show 80 --require-stage17
```

**Interpretation:** `0x1712` with no `0x1713` isolates the first
RAM loop entry; `0x1713` without `0x1714` isolates the bank values
read/emit; `0x1714` and `0x1718` without later heartbeats isolates
the USB polling/runtime path. `0x17ff` would prove that the entire
probe completed. Use the separate session files and Windows timestamps
to tell repeat boot attempts apart. Inspect the live bank and
reservation numbers before selecting an XNU load address.
