/* Offline-built, bounded Stage 14 USB event-stream probe. No XNU. */
#include <stdint.h>
#include "cactus_usb.h"
#include "cactus_trace.h"

static uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

void stage14_usb_probe(uint32_t *diag)
{
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (!frequency) frequency = 13000000u;
    cactus_trace_reset(0xca140001u);
    cactus_trace_emit(0x1400, 0, frequency, 0xff007f80u);
    /* Queue real, changing frames even before DTR. */
    for (unsigned i = 1; i <= 16u; ++i)
        cactus_trace_emit(0x1401, i, ((uint64_t)i << 32) | (0xaa00u + i),
                          i & 1u ? 0xffffffffffffffffull : 0);
    if (!usb_init(diag)) return;
    cactus_trace_emit(0x1402, diag[1], diag[2], diag[3]);

    uint32_t generated = 0;
    const uint64_t start = ticks();
    uint64_t last_sample = start;
    while (ticks() - start < frequency * 25u) {
        usb_poll();
        uint64_t now = ticks();
        /* Changes are visible in payload, sequence, timestamp and checksum. */
        if (now - last_sample >= frequency / 10u) {
            last_sample = now;
            if (generated < 240u) {
                cactus_trace_emit(0x1403, generated,
                    0x8040201000ff00aaull ^ (uint64_t)generated,
                    ((uint64_t)diag[8] << 32) |
                    cactus_trace_dropped());
                ++generated;
            }
        }
        const uint8_t *frame = cactus_trace_peek();
        if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
            cactus_trace_pop();
    }
    cactus_trace_emit(0x14ff, generated, cactus_trace_dropped(),
                      cactus_trace_pending());
    const uint64_t drain_start = ticks();
    while (ticks() - drain_start < frequency * 2u &&
           cactus_trace_pending()) {
        usb_poll();
        const uint8_t *frame = cactus_trace_peek();
        if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
            cactus_trace_pop();
    }
    diag[12] = cactus_trace_dropped();
    diag[13] = 14;
    diag[14] = cactus_trace_pending();
    usb_shutdown();
}
