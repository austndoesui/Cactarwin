#!/usr/bin/env bash
# Build an offline-only cactus stage probe that returns to TWRP with a
# live initramfs trace file. Do not flash during the diagnostic build.
set -euo pipefail
cd "$(dirname "$0")"

STAGE="${1:-3}"
case "$STAGE" in
    3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19) ;;
    *) echo "usage: $0 [3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19]" >&2; exit 2 ;;
esac

OUT="out/cactus/linux-chainload"
STOCK="$OUT/recovery-ramdisk.gz"
DTB="$OUT/recovery-dtb"
DTBO="/home/lapdq/cactus-kernel/images/cactus_dtbo.img"
MKBOOT="/home/lapdq/cactus-kernel/pack/mkbootimg.py"
for f in "$STOCK" "$DTB" "$DTBO" "$MKBOOT" "$OUT/recovery-Image"; do
    test -f "$f" || { echo "missing: $f" >&2; exit 1; }
done

PAYLOAD_DEFINE=()
if [[ "$STAGE" == 18 || "$STAGE" == 19 ]]; then
    test -f out/cactus/payload.bin || { echo "missing: out/cactus/payload.bin" >&2; exit 1; }
    python3 prepare_stage18_payload.py out/cactus/payload.bin "$OUT/stage18-payload.bin"
    python3 trace_initramfs.py prepare "$STOCK" "$OUT/trace-ramdisk.bin" \
        "$OUT/trace-address.txt" --payload "$OUT/stage18-payload.bin" \
        --payload-address-file "$OUT/stage18-address.txt"
    read -r PAYLOAD_ADDR PAYLOAD_BYTES < "$OUT/stage18-address.txt"
    PAYLOAD_DEFINE=(-DPAYLOAD_ADDR="$PAYLOAD_ADDR" -DPAYLOAD_LENGTH="$PAYLOAD_BYTES")
else
    python3 trace_initramfs.py prepare "$STOCK" "$OUT/trace-ramdisk.bin" "$OUT/trace-address.txt"
fi
TRACE_ADDR="$(cat "$OUT/trace-address.txt")"
PREFIX="$OUT/trace-stage-$STAGE"
CFLAGS=(-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only
        -mstrict-align -fno-stack-protector -DBOARD_CACTUS)

aarch64-linux-gnu-gcc "${CFLAGS[@]}" -x assembler-with-cpp -DSTAGE="$STAGE" \
    -DTRACE_ADDR="$TRACE_ADDR" -c linux_stage_probe.S -o "$PREFIX.o"
OBJECTS=("$PREFIX.o")
if [[ "$STAGE" == 4 ]]; then
    aarch64-linux-gnu-gcc "${CFLAGS[@]}" -c debug.c -o "$PREFIX-debug.o"
    OBJECTS+=("$PREFIX-debug.o")
fi
if [[ "$STAGE" == 6 ]]; then
    aarch64-linux-gnu-gcc "${CFLAGS[@]}" -c pmsg_trace.c -o "$PREFIX-pmsg.o"
    OBJECTS+=("$PREFIX-pmsg.o")
fi
if [[ "$STAGE" -ge 8 ]]; then
  if [[ "$STAGE" == 14 || "$STAGE" == 15 || "$STAGE" == 16 || "$STAGE" == 17 || "$STAGE" == 18 || "$STAGE" == 19 ]]; then
    SOURCES=(cactus_usb cactus_trace)
    EXTRA_DEFINES=()
    if [[ "$STAGE" == 15 ]]; then
      SOURCES+=(stage15_early)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=15 -DSHIM_STAGE15_EARLY=1)
    elif [[ "$STAGE" == 16 ]]; then
      # Actual cactus shim FDT and reserved-RAM debug routines, but no
      # loader or XNU branch. Dead FDT framebuffer functions are GC'd.
      SOURCES+=(stage16_probe fdt fdt_ranges debug)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=16 -ffunction-sections -fdata-sections)
    elif [[ "$STAGE" == 17 ]]; then
      SOURCES+=(stage17_probe fdt fdt_ranges)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=17 -ffunction-sections -fdata-sections)
    elif [[ "$STAGE" == 18 ]]; then
      SOURCES+=(stage18_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=18 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 19 ]]; then
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=19 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    else
      SOURCES+=(stage14_probe)
    fi
    for source in "${SOURCES[@]}"; do
      aarch64-linux-gnu-gcc "${CFLAGS[@]}" -Wall -Wextra -Werror \
          "${EXTRA_DEFINES[@]}" \
          -c "$source.c" -o "$PREFIX-$source.o"
      OBJECTS+=("$PREFIX-$source.o")
    done
  else
    USB_PROBE_LEVEL=2
    if [[ "$STAGE" == 9 ]]; then USB_PROBE_LEVEL=0; fi
    if [[ "$STAGE" == 10 ]]; then USB_PROBE_LEVEL=1; fi
    if [[ "$STAGE" == 11 ]]; then USB_PROBE_LEVEL=3; fi
    if [[ "$STAGE" == 12 ]]; then USB_PROBE_LEVEL=4; fi
    if [[ "$STAGE" == 13 ]]; then USB_PROBE_LEVEL=5; fi
    aarch64-linux-gnu-gcc "${CFLAGS[@]}" -Wall -Wextra -Werror \
        -DUSB_PROBE_LEVEL="$USB_PROBE_LEVEL" \
        -c musb_cdc_probe.c -o "$PREFIX-musb.o"
    OBJECTS+=("$PREFIX-musb.o")
  fi
fi
if [[ "$STAGE" == 16 || "$STAGE" == 17 || "$STAGE" == 18 || "$STAGE" == 19 ]]; then
  aarch64-linux-gnu-ld --build-id=none -N -e _start --gc-sections -Ttext=0 "${OBJECTS[@]}" -o "$PREFIX.elf"
else
  aarch64-linux-gnu-ld --build-id=none -N -Ttext=0 "${OBJECTS[@]}" -o "$PREFIX.elf"
fi
aarch64-linux-gnu-objcopy -O binary "$PREFIX.elf" "$PREFIX-Image"

# The C function for stage 4 is linked after the probe assembly's end label.
# Correct the Linux arm64 Image header to cover the complete emitted code.
python3 - "$PREFIX-Image" <<'PY'
import struct
import sys
from pathlib import Path
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
assert data[0x38:0x3c] == bytes((65, 82, 77, 100))
struct.pack_into('<Q', data, 0x10, len(data))
path.write_bytes(data)
print('probe Image', len(data), 'bytes')
PY

gzip -9 -c "$PREFIX-Image" > "$PREFIX.gz"
cat "$PREFIX.gz" "$DTB" > "$PREFIX.gz-dtb"
IMAGE="$PWD/out/cactus/cactus-trace-stage-$STAGE-boot.img"
if [[ "$STAGE" == 18 ]]; then
  # Keep the first hardware-tested Stage18 image intact for comparing
  # its reproducible unaligned-payload data abort with this fix.
  IMAGE="$PWD/out/cactus/cactus-trace-stage-18-aligned-boot.img"
fi
python3 "$MKBOOT" --header_version 2 \
    --kernel "$PREFIX.gz-dtb" --ramdisk "$OUT/trace-ramdisk.bin" --dtb "$DTBO" \
    --base 0x40078000 --kernel_offset 0x00008000 \
    --ramdisk_offset 0x11A88000 --second_offset 0x000E8000 \
    --tags_offset 0x07808000 --dtb_offset 0x07808000 \
    --pagesize 2048 --os_version 11.0.0 \
    --cmdline "bootopt=64S3,32N2,64N2 androidboot.selinux=permissive buildvariant=user" \
    -o "$IMAGE"
python3 check_image.py "$IMAGE"
sha256sum "$IMAGE"
echo "OFFLINE image: $IMAGE"
echo "After an approved test that reaches TWRP:"
echo "  adb exec-out cat /cactus-trace.bin > cactus-trace.bin"
echo "  python3 shim/trace_initramfs.py decode cactus-trace.bin"
