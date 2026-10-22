#include "payload_layout.h"
#include <stddef.h>
#include <stdint.h>

_Static_assert(offsetof(struct xnu_payload, segs) == XNU_PAYLOAD_HEADER_BYTES,
               "SWEETXNU ABI header");
_Static_assert(sizeof(struct xnu_payload_segment) == 32, "segment ABI");

static int inside(uint64_t start, uint64_t len, uint64_t base, uint64_t size)
{
    return len && start >= base && start - base <= size &&
           len <= size - (start - base);
}

static int add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (b > UINT64_MAX - a) return 0;
    *out = a + b;
    return 1;
}

enum xnu_validation xnu_validate_payload(
    const void *payload, uint64_t bytes, const struct fdt_info *live,
    uint64_t dtb, uint64_t dtb_size, uint64_t shim, uint64_t shim_size,
    struct xnu_layout *result)
{
    const struct xnu_payload *p = payload;
    if (!payload || !live || !result || bytes < XNU_PAYLOAD_HEADER_BYTES)
        return XNU_VALIDATE_BAD_HEADER;
    static const char magic[8] = {'S','W','E','E','T','X','N','U'};
    for (unsigned i = 0; i < 8; ++i)
        if (p->magic[i] != magic[i]) return XNU_VALIDATE_BAD_HEADER;
    if (p->version != 1 || !p->nsegs || p->nsegs > 64)
        return XNU_VALIDATE_BAD_HEADER;
    if ((uint64_t)p->nsegs * 32 > bytes - XNU_PAYLOAD_HEADER_BYTES)
        return XNU_VALIDATE_BAD_TABLE;
    if ((p->phys_base & (XNU_PAGE_BYTES - 1)) || !p->mem_size ||
        (p->mem_size & (XNU_PAGE_BYTES - 1)))
        return XNU_VALIDATE_UNSAFE_RAM;
    if (!inside(p->phys_base, p->mem_size, p->phys_base, p->mem_size) ||
        !fdt_region_free(live, p->phys_base, p->mem_size))
        return XNU_VALIDATE_UNSAFE_RAM;
    if (fdt_region_overlap(p->phys_base, p->mem_size, dtb, dtb_size) ||
        fdt_region_overlap(p->phys_base, p->mem_size,
                           live->initrd_start,
                           live->initrd_end > live->initrd_start ?
                           live->initrd_end - live->initrd_start : 0) ||
        fdt_region_overlap(p->phys_base, p->mem_size, shim, shim_size))
        return XNU_VALIDATE_BOOT_OVERLAP;

    uint64_t adt_end, kernel_off;
    if ((p->adt_off & 7u) || p->adt_size < 44 ||
        !add_ok(p->adt_off, p->adt_size, &adt_end) ||
        adt_end > bytes || p->kernel_end < p->virt_base)
        return XNU_VALIDATE_BAD_ADT;
    /* Apple ADT root has LE properties/children and a 'name' property. */
    const uint8_t *a = (const uint8_t *)payload + p->adt_off;
    if (a[8] != 'n' || a[9] != 'a' || a[10] != 'm' || a[11] != 'e' ||
        a[12] != 0 || (a[0] | a[1] | a[2] | a[3]) == 0)
        return XNU_VALIDATE_BAD_ADT;
    if (!add_ok(p->kernel_end - p->virt_base, 0, &kernel_off) ||
        kernel_off >= p->mem_size)
        return XNU_VALIDATE_BOOTARGS;

    uint64_t max_end = 0, entry_offset = 0;
    int found_entry = 0;
    if (p->entry_va < p->virt_base) return XNU_VALIDATE_ENTRY;
    entry_offset = p->entry_va - p->virt_base;
    for (uint32_t i = 0; i < p->nsegs; ++i) {
        const struct xnu_payload_segment *s = &p->segs[i];
        uint64_t file_end, offset, segment_end;
        if (!s->vmsize || s->file_size > s->vmsize ||
            (s->file_off & 7u) ||
            !add_ok(s->file_off, s->file_size, &file_end) ||
            file_end > bytes || s->vmaddr < p->virt_base)
            return XNU_VALIDATE_SEGMENT;
        offset = s->vmaddr - p->virt_base;
        if (!add_ok(offset, s->vmsize, &segment_end) ||
            segment_end > p->mem_size)
            return XNU_VALIDATE_SEGMENT;
        if (segment_end > max_end) max_end = segment_end;
        if (entry_offset >= offset &&
            entry_offset - offset < s->file_size) found_entry = 1;
        for (uint32_t j = 0; j < i; ++j)
            if (fdt_region_overlap(s->vmaddr, s->vmsize,
                                   p->segs[j].vmaddr, p->segs[j].vmsize))
                return XNU_VALIDATE_SEGMENT_OVERLAP;
    }
    if (!found_entry) return XNU_VALIDATE_ENTRY;
    if (kernel_off < max_end) return XNU_VALIDATE_BOOTARGS;
    uint64_t adt_pa, adt_pa_end, ba_pa, top, entry_pa;
    if (!add_ok(p->phys_base, kernel_off, &adt_pa) ||
        !add_ok(adt_pa, p->adt_size, &adt_pa_end) ||
        adt_pa_end > UINT64_MAX - (XNU_PAGE_BYTES - 1) ||
        !add_ok(p->phys_base, entry_offset, &entry_pa))
        return XNU_VALIDATE_BOOTARGS;
    ba_pa = (adt_pa_end + XNU_PAGE_BYTES - 1) & ~(XNU_PAGE_BYTES - 1);
    if (!add_ok(ba_pa, XNU_PAGE_BYTES, &top) ||
        !inside(adt_pa, p->adt_size, p->phys_base, p->mem_size) ||
        !inside(ba_pa, XNU_PAGE_BYTES, p->phys_base, p->mem_size) ||
        !inside(entry_pa, 1, p->phys_base, p->mem_size))
        return XNU_VALIDATE_BOOTARGS;
    result->entry_pa = entry_pa;
    result->adt_pa = adt_pa;
    result->boot_args_pa = ba_pa;
    result->top = top;
    result->phys_base = p->phys_base;
    result->mem_size = p->mem_size;
    result->segments = p->nsegs;
    return XNU_VALIDATE_OK;
}
