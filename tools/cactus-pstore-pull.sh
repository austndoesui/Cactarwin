#!/bin/sh
# Dump the CACTUS shim pstore region through TWRP or rooted Android.
#
#   cactus-pstore-pull.sh [OUT.bin]
#   cactus-pstore.py OUT.bin
#
# The region is reserved-memory@47c90000 (0xe0000).  /dev/mem must be
# readable by the adb shell. The current cactus TWRP does NOT support
# /dev/mem (ENXIO); use only with a purpose-built recovery kernel that
# exposes physical RAM. This script does not enable /dev/mem.
set -eu

OUT="${1:-cactus-pstore.bin}"
BASE=$((0x47c90000))
SIZE=$((0xe0000))
BLOCK=4096
SKIP=$((BASE / BLOCK))
COUNT=$((SIZE / BLOCK))

adb wait-for-device
adb root >/dev/null 2>&1 || true
adb wait-for-device

if ! adb shell "test -r /dev/mem" >/dev/null 2>&1; then
	echo "error: /dev/mem is not readable by adb shell" >&2
	exit 1
fi

adb exec-out "dd if=/dev/mem bs=$BLOCK skip=$SKIP count=$COUNT 2>/dev/null" > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)" >&2
