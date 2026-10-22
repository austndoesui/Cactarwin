/*
 * Stage 18: exercise the exact payload admission routine used by loader_boot
 * while retaining the Stage17 entry, MUSB logger and TWRP recovery chainload.
 * No segment copies, MMU switches or XNU execution in this first test.
 */
#include <stdint.h>
#include "cactus_trace.h"
#include "cactus_usb.h"
#include "fdt.h"
#include "fdt_ranges.h"
#include "payload_layout.h"

#ifndef PAYLOAD_ADDR
#error Stage18 requires payload physical address from appended CPIO
#endif
#ifndef PAYLOAD_LENGTH
#error Stage18 requires payload byte length
#endif

enum {
    S18_ENTRY = 0x1800, S18_USB = 0x1801, S18_BEAT = 0x1802,
    S18_DTB = 0x1810, S18_FDT = 0x1811,
    S18_PAYLOAD = 0x1820, S18_RAM = 0x1821,
    S18_CHECK_BEFORE = 0x1830, S18_CHECK_AFTER = 0x1831,
    S18_SEGMENT = 0x1840, S18_READY = 0x18f0,
    S18_DONE = 0x18ff, S18_FAULT = 0x18ee,
};
static uint32_t usb_diag[16];
static uint64_t frequency;
static unsigned online, faulted, heartbeats;

extern char _start[], image_end[];

static uint64_t ticks(void)
{
    uint64_t x;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(x));
    return x;
}

static void mark(unsigned event, uint64_t a, uint64_t b, uint64_t c)
{
    (void)cactus_trace_emit(event, a, b, c);
}

static void pump(unsigned milliseconds)
{
    const uint64_t start = ticks(), period = frequency / 1000u * milliseconds;
    uint64_t heartbeat_at = start;
    while (ticks() - start < period) {
        if (online) {
            usb_poll();
            if (usb_online() && (usb_diag[8] & 1u)) {
                const uint8_t *frame = cactus_trace_peek();
                if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
                    cactus_trace_pop();
            }
        }
        uint64_t now = ticks();
        if (now - heartbeat_at >= frequency) {
            heartbeat_at = now;
            mark(S18_BEAT, heartbeats++, usb_diag[8],
                 ((uint64_t)cactus_trace_dropped() << 32) |
                 cactus_trace_pending());
        }
    }
}

void stage18_probe(uintptr_t dtb)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (!frequency) frequency = 13000000u;
    online = faulted = heartbeats = 0;
    cactus_trace_reset(0xca180001u);
    mark(S18_ENTRY, dtb, el, frequency);
    online = usb_init(usb_diag);
    mark(S18_USB, online, usb_diag[1], usb_diag[2]);
    if (!online) return;

    uint32_t fdt_bytes = 0;
    int valid_header = fdt_check(dtb, &fdt_bytes);
    mark(S18_DTB, valid_header, dtb, fdt_bytes);
    struct fdt_info live;
    int valid_fdt = valid_header && fdt_bytes <= 0x100000u &&
        fdt_inspect((const void *)dtb, fdt_bytes, &live) == 0;
    mark(S18_FDT, valid_fdt,
         valid_fdt ? live.memory_count : 0,
         valid_fdt ? live.reserved_count : 0);
    if (!valid_fdt) {
        mark(S18_DONE, XNU_VALIDATE_BAD_HEADER, 0, 0);
        pump(10000);
        usb_shutdown();
        return;
    }

    const struct xnu_payload *payload =
        (const struct xnu_payload *)(uintptr_t)PAYLOAD_ADDR;
    /* The ramdisk marker was verified by assembly before reaching us. */
    if (PAYLOAD_LENGTH < XNU_PAYLOAD_HEADER_BYTES ||
        ((uintptr_t)payload & 15u)) {
        mark(S18_DONE, XNU_VALIDATE_BAD_HEADER, PAYLOAD_LENGTH, 0);
        pump(5500);
        usb_shutdown();
        return;
    }
    mark(S18_PAYLOAD, payload->phys_base, payload->mem_size, payload->nsegs);
    mark(S18_RAM, fdt_region_free(&live, payload->phys_base,
                                 payload->mem_size),
         (uintptr_t)PAYLOAD_ADDR, PAYLOAD_LENGTH);
    mark(S18_CHECK_BEFORE, (uintptr_t)payload, (uintptr_t)_start,
         (uintptr_t)image_end - (uintptr_t)_start);
    struct xnu_layout result;
    enum xnu_validation status = xnu_validate_payload(
        payload, PAYLOAD_LENGTH, &live, dtb, fdt_bytes,
        (uintptr_t)_start,
        (uintptr_t)image_end - (uintptr_t)_start, &result);
    mark(S18_CHECK_AFTER, status, payload->phys_base, payload->mem_size);
    if (status == XNU_VALIDATE_OK) {
        /* All segments are in an admitted non-reserved physical window.
         * Emit the actual loader offsets, without writing destination RAM. */
        for (unsigned i = 0; i < result.segments; ++i) {
            const struct xnu_payload_segment *s = &payload->segs[i];
            mark(S18_SEGMENT, i, result.phys_base +
                 (s->vmaddr - payload->virt_base), s->vmsize);
        }
        mark(S18_READY, result.entry_pa, result.boot_args_pa, result.top);
    }
    /* Emit DONE before the known ~9-second USB cutoff on past stages. */
    mark(S18_DONE, status, cactus_trace_pending(), cactus_trace_dropped());
    /* Previous Stage17/18 traces stopped after ~9s of polling, likely
     * before the TWRP chainload. Return earlier and keep diagnostics
     * queued at least 5.5 seconds for the host's USB CDC connection. */
    pump(5500);
    usb_shutdown();
    online = 0;
}

void stage18_fault(uint64_t esr, uint64_t elr, uint64_t far)
{
    if (faulted)
        for (;;) __asm__ volatile("wfe");
    faulted = 1;
    mark(S18_FAULT, esr, elr, far);
    if (online) pump(2500);
    for (;;) __asm__ volatile("wfe");
}
