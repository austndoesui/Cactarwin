/* SPDX-License-Identifier: MIT */
#ifndef CACTUS_FDT_RANGES_H
#define CACTUS_FDT_RANGES_H
#include <stddef.h>
#include <stdint.h>
#define FDT_MAX_MEMORY 8
#define FDT_MAX_RESERVED 64
struct fdt_range { uint64_t base, size; };
struct fdt_info {
    uint32_t bytes;
    unsigned memory_count, reserved_count;
    struct fdt_range memory[FDT_MAX_MEMORY], reserved[FDT_MAX_RESERVED];
    uint64_t initrd_start, initrd_end;
};
/* Read-only, bounded inspection. Does not allocate or authorize a memory map. */
int fdt_inspect(const void *data, size_t available, struct fdt_info *info);
/* True only if all bytes lie in an advertised DRAM bank and are outside
 * every FDT memreserve and reserved-memory child range. */
int fdt_region_free(const struct fdt_info *info, uint64_t base, uint64_t size);
int fdt_region_overlap(uint64_t a, uint64_t alen, uint64_t b, uint64_t blen);
#endif
