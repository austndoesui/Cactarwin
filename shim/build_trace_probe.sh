#!/usr/bin/env bash
# Build an offline-only cactus stage probe that returns to TWRP with a
# live initramfs trace file. Do not flash during the diagnostic build.
set -euo pipefail
cd "$(dirname "$0")"

STAGE="${1:-3}"
case "$STAGE" in
    3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30) ;;
    *) echo "usage: $0 [3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30]" >&2; exit 2 ;;
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
if [[ "$STAGE" -ge 18 ]]; then
    test -f out/cactus/payload.bin || { echo "missing: out/cactus/payload.bin" >&2; exit 1; }
    python3 prepare_stage18_payload.py out/cactus/payload.bin "$OUT/stage18-payload.bin"
    if [[ "$STAGE" == 21 ]]; then
        python3 check_stage20b_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 22 ]]; then
        python3 check_stage20c_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 23 ]]; then
        python3 check_stage20d_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 24 ]]; then
        python3 check_stage21c1_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 25 ]]; then
        python3 check_stage21c2_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 26 ]]; then
        python3 check_stage21d_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 27 ]]; then
        python3 check_stage22_entry.py "$OUT/stage18-payload.bin"
    elif [[ "$STAGE" == 28 || "$STAGE" == 29 || "$STAGE" == 30 ]]; then
        python3 check_stage22_entry.py "$OUT/stage18-payload.bin"
        if [[ "$STAGE" == 30 ]]; then
            python3 check_stage25_pmap.py "$OUT/stage18-payload.bin"
        fi
    fi
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
# Stages 19+ must call the USB/XNU diagnostic probe from the live entry
# wrapper. If the preprocessor omits that branch, the image silently boots
# TWRP with no COM25 frames and an untouched CTRC header.
if [[ "$STAGE" -ge 19 ]]; then
    if ! aarch64-linux-gnu-readelf -r "$PREFIX.o" |
         grep -Eq 'R_AARCH64_CALL26[[:space:]]+[^[:space:]]+[[:space:]]+stage19_probe'; then
        echo "stage $STAGE entry does not call stage19_probe" >&2
        exit 1
    fi
fi
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
  if [[ "$STAGE" -ge 14 ]]; then
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
    elif [[ "$STAGE" == 20 ]]; then
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=20 -DCACTUS_STAGE20=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 21 ]]; then
      # Stage20-B uses its own image so the Stage20-A hardware image survives.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=21 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 22 ]]; then
      # Stage20-C runs the known early framebuffer code until XNU's VBAR write.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=22 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 23 ]]; then
      # Stage20-D runs real XNU VBAR handoff through a reversible vector stub.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=23 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 24 ]]; then
      # Stage21-C1: MMU-off EL1t trap; use HVC and EL2's physical stack.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=24 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 25 ]]; then
      # Stage21-C2: first translated instruction and virtual EL1t vector.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=25 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 26 ]]; then
      # Stage21-D: trap/fault after virtual C prologue, preserve EL2 USB return.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=26 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_STAGE21D=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 27 ]]; then
      # Combined Stage22: let XNU run through early relocations and init
      # calls, capture the first real fault or the later C milestone.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=27 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_STAGE21D=1 -DCACTUS_STAGE22=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 28 ]]; then
      # Stage23: retain the Stage22 hardware-proven admission and vector
      # checks, run XNU without a synthetic trap, and retain fault logs.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=28 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_STAGE21D=1 -DCACTUS_STAGE22=1 -DCACTUS_STAGE23=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 29 ]]; then
      # Stage24: native Stage23 XNU continuation with original XNU
      # register snapshot on first EL1 sync HVC for panic provenance.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=29 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_STAGE21D=1 -DCACTUS_STAGE22=1 -DCACTUS_STAGE23=1 -DCACTUS_STAGE24=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
    elif [[ "$STAGE" == 30 ]]; then
      # Stage25: trap exactly where pmap_map_bd would panic for a null
      # kernel page-table entry and capture its VA/PA range and descriptors.
      SOURCES+=(stage19_probe fdt fdt_ranges payload_layout)
      EXTRA_DEFINES=(-DCACTUS_TRACE_STAGE=30 -DCACTUS_STAGE20=1 -DCACTUS_STAGE20B=1 -DCACTUS_STAGE20C=1 -DCACTUS_STAGE20D=1 -DCACTUS_STAGE21C1=1 -DCACTUS_STAGE21C2=1 -DCACTUS_STAGE21D=1 -DCACTUS_STAGE22=1 -DCACTUS_STAGE23=1 -DCACTUS_STAGE24=1 -DCACTUS_STAGE25=1 -DCACTUS_TRACE_SLOTS=64 -ffunction-sections -fdata-sections "${PAYLOAD_DEFINE[@]}")
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
if [[ "$STAGE" -ge 16 ]]; then
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
