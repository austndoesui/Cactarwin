/* Shared, read-only SWEETXNU admission check for cactus loader and USB probes. */
#pragma once
#include <stdint.h>
#include "fdt_ranges.h"

#define XNU_PAYLOAD_HEADER_BYTES 0x2a8u
#define XNU_PAGE_BYTES 0x4000ull

struct xnu_payload_segment {
    uint64_t vmaddr, vmsize, file_off, file_size;
};
struct xnu_payload {
    char magic[8];
    uint32_t version, nsegs;
    uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
    uint64_t phys_base, mem_size;
    char cmdline[608];
    struct xnu_payload_segment segs[];
};
struct xnu_layout {
    uint64_t entry_pa, adt_pa, boot_args_pa, top;
    uint64_t phys_base, mem_size;
    uint32_t segments;
};
enum xnu_validation {
    XNU_VALIDATE_OK = 0,
    XNU_VALIDATE_BAD_HEADER = 1,
    XNU_VALIDATE_BAD_TABLE = 2,
    XNU_VALIDATE_BAD_ADT = 3,
    XNU_VALIDATE_UNSAFE_RAM = 4,
    XNU_VALIDATE_BOOT_OVERLAP = 5,
    XNU_VALIDATE_SEGMENT = 6,
    XNU_VALIDATE_SEGMENT_OVERLAP = 7,
    XNU_VALIDATE_ENTRY = 8,
    XNU_VALIDATE_BOOTARGS = 9,
};
/* Fail closed. No payload/firmware memory is written by this function. */
enum xnu_validation xnu_validate_payload(
    const void *payload, uint64_t bytes, const struct fdt_info *live,
    uint64_t dtb, uint64_t dtb_size, uint64_t shim, uint64_t shim_size,
    struct xnu_layout *result);
