# XNU on Xiaomi Redmi 6A (cactus)

Experimental bring-up of Apple's open-source XNU kernel on the **Redmi 6A (`cactus`)**, using the MediaTek MT6761/MT6765 platform. This repository contains the board-adapted XNU patch stack, Android boot-image shim, USB diagnostic firmware, and host-side checks.

**Status (23 October 2026): early XNU instructions run on the phone, but XNU has not booted an OS or displayed an interface.** Stage 25 captured 77 CRC-valid USB frames and identified the current framebuffer page-table blocker.

| Milestone | Evidence |
| --- | --- |
| Stock LK → Android boot v2 → shim | Proven on Redmi 6A |
| Raw USB CDC logging | Proven over USB serial (COM number varies by host) |
| EL2 → EL1 and real XNU instructions | Stages 20–22 captured |
| Genuine XNU panic | Stages 23–24: `pmap_map_bd()` |
| Exact missing mapping | Stage 25: zero L1 descriptor for VA `0xfffffff04a000000` |
| Full XNU boot / on-screen console / iOS userland | **Not achieved** |

## Next blocker

**Stage 26 is planned, not built.** Correct XNU's early virtual address reservation and page-table allocation before framebuffer `io_map()` crosses a 1 GiB L1 boundary. Stage 25 measured framebuffer physical range `0x5e605000..0x5e9fa000`, initial VA `0xfffffff049dbb000`, and an absent L1 descriptor at `0x824c1e08`. See `osfmk/arm64/arm_vm_init.c` near `va_l1_end`. Keep COM25 exception logging through the next experiment.

The bootloader shows the **MI logo** during USB logging. Test pixels written to framebuffer RAM were **not visible** on the display, so actual LK scanout and an on-screen console remain unresolved.

## Repository layout

| Path | Purpose |
| --- | --- |
| [shim/](shim/) | Shim source, boot-image builder, USB logger, trace decoder, stage verifiers/tests |
| [xnu/patches/](xnu/patches/) | Ordered XNU patch series, including Cactus-specific additions |
| [xnu/README.md](xnu/README.md) | Cactus XNU configuration and build background |
| [tools/](tools/) | ADT/payload/boot tooling, including inherited legacy utilities |
| [NOTES.md](NOTES.md) | **Current** tested state and next engineering task |
| [shim/TRACE-STAGE25.md](shim/TRACE-STAGE25.md) | Hardware-proven missing mapping and diagnostic protocol |
| [docs/CACTUS-BRINGUP-HISTORY.md](docs/CACTUS-BRINGUP-HISTORY.md) | Historical Cactus experiments (including superseded plans) |
| [BENESSE-CACTUS-PORT-PLAN.md](BENESSE-CACTUS-PORT-PLAN.md) | Reference MTK Benesse comparison |

Stage names and numeric image builds differ: **Stage 25** uses **build stage 30**.

## Local build and debugging

The tested host uses **Windows PowerShell + WSL Ubuntu**, Python 3, ARM64 cross-tools, and existing local Cactus boot/recovery packaging inputs. Compiled kernels, firmware dumps, device traces, and diagnostic boot images are **not stored in Git**.

To rebuild the last tested Stage 25 diagnostic image from a similarly prepared WSL environment (adjust the checkout path):

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd "/mnt/f/ROM/patching/redmi 6a xnu/ios-cactus/shim" && ./build_trace_probe.sh 30'
```

The build requires locally generated assets in `shim/out/cactus/` and external board packaging inputs. A clean clone alone does **not** produce a flashable image.

To decode a previously captured USB session:

```powershell
python shim/decode-cactus-usb.py "path/to/session.bin" --require-stage25 --show 90
```

Start the capture process **before** booting the device:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\shim\watch-cactus-usb.ps1 -Seconds 180
```

The diagnostic interface uses USB VID `1209`, PID `0001`. The COM port number is assigned by Windows.

### Hardware testing

Verify `fastboot getvar product` says `cactus` and confirm the boot-image hash. On this handset, `fastboot boot IMAGE` has previously booted the **installed** boot partition instead of the downloaded image. Proven tests use `fastboot flash boot IMAGE` and `fastboot reboot`. Existing rollback images are maintained by the device owner. From fastboot, `fastboot oem reboot-recovery` boots TWRP when accessible.

The Stage 23–25 fault probes intentionally hold COM25 after an exception, sometimes requiring hardware buttons to re-enter fastboot.

## XNU sources and lineage

This project originated from an earlier `ios-sweet` codebase. Many inherited XNU patches and utility filenames retain `sweet` because they are still part of a numbered build sequence. Those names **do not establish Cactus hardware compatibility**. Cactus-specific additions begin at `0082-cactus-machine-config.patch`, followed by the 16K software VM page / 4K hardware page alignment fixes. See [xnu/README.md](xnu/README.md).

The GitHub XNU workflow still defaults to the older machine configuration. **Pass `machine_config=CACTUS` explicitly** when triggering a kernel build:

```sh
gh workflow run xnu.yml --ref main -f machine_config=CACTUS -f kernel_config=DEVELOPMENT
```

The shim's diagnostic stage builds and checks are performed **locally**.

## Scope

This is experimental kernel/platform research. The Redmi 6A has **no verified bootable iOS GUI, complete XNU startup, on-screen diagnostic console, or working iOS radio/storage stack**. Qualcomm, UFS, and Redmi Note 10 Pro claims from the ancestor project are not results for this device.
