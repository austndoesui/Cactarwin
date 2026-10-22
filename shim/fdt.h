#pragma once
#include <stdint.h>

/* Validate the FDT header and return its total size. Returns 0 if invalid. */
int fdt_check(uintptr_t fdt, uint32_t *total_out);

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);

/* Print /chosen, /memory and /reserved-memory entries to the framebuffer. */
void fdt_dump(uintptr_t fdt);
