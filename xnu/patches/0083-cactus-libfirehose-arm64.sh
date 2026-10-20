#!/bin/bash
# libfirehose_kernel is built by dxb's xcodebuild, not by the XNU makefiles, so
# it does not see CACTUS's -mcpu=cortex-a53 and clang emits LSE atomics
# (LDADD/CAS) that a Cortex-A53 cannot execute. Build the arm64 slice for the
# A53; the CACTUS kernel only links that slice.
set -euo pipefail

[[ "${MACHINE_CONFIG:-}" == CACTUS ]] || exit 0

build="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build.sh"
[[ -f "${build}" ]] || { echo "missing ${build}" >&2; exit 1; }

old='ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"'
new='ARCHS="arm64" VALID_ARCHS="arm64" OTHER_CFLAGS="-mcpu=cortex-a53"'
grep -qF "${old}" "${build}"
sed -i '' "s|${old}|${new}|" "${build}"
grep -qF "${new}" "${build}"
