# XNU kernel configuration for Cactus

The kernel target is **XNU (xnu-8796 era)** running on the MediaTek Redmi 6A (`cactus`). Cactus support is under bring-up. The phone has executed real XNU instructions and exposed the first memory-mapping panic using the USB diagnostic shim, but it has **not** completed kernel startup.

## Patch series and origins

The numbered files in [patches/](patches/) are applied in order by the build tooling. Many early files have `sweet` in their name: they were inherited from the ancestor development tree, and some supply general upstream compatibility fixes or libraries. Do **not** infer those files have been independently hardware-validated on this MediaTek board, or change patch filenames casually: scripts depend on their ordering.

Board-specific additions include:

- `0082-cactus-machine-config.patch`: machine target, CPU/MTK configuration and initial Cactus changes.
- `0084-cactus-16k-pte-align.patch`: alignment for experimental 16 KiB XNU VM pages over a 4 KiB hardware granule.
- `0085-cactus-commpage-16k-align.patch`: aligned randomized commpage mapping.

The XNU image used in the Stage 25 experiments is currently an **external/local compiled payload**; stage builds consume a generated `shim/out/cactus/payload.bin` and preserve its Mach-O information for symbol lookup and verifying instructions.

## Build configuration

[GitHub workflow](../.github/workflows/xnu.yml) uses a pinned `blacktop/darwin-xnu-build` runner stack with a macOS 13.5 / XNU-8796 generation baseline. The workflow still defaults to the original machine config for backward compatibility. **For Cactus, always pass `machine_config=CACTUS`:**

```sh
gh workflow run xnu.yml --ref main -f machine_config=CACTUS -f kernel_config=DEVELOPMENT
```

The project also has host-side local tools for replaying small compilations and verifying the packed boot stages. A successful kernel compile alone does not demonstrate any Cactus hardware milestone.

## Current source fix target

The Stage 25 COM25 capture showed a null pmap L1 entry at VA `0xfffffff04a000000` while `pmap_map_bd()` maps framebuffer PA `0x5e605000..0x5e9fa000`. The current hypothesis to test is that `osfmk/arm64/arm_vm_init.c` computed an insufficient early virtual mapping window near `va_l1_end`. Audit the actual early `io_map` allocation range and add the required page tables correctly. Rebuild and prove a genuine execution advance with the USB trace.

See [active handoff](../NOTES.md), [hardware trace](../shim/TRACE-STAGE25.md), and [historical Cactus bring-up](../docs/CACTUS-BRINGUP-HISTORY.md).
