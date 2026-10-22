/*
 * Stage 17: prioritize the live FDT RAM map before the repeatable
 * Stage 16 ~9s cutoff. Keep the proven recovery-wrapped USB entry.
 *
 * Read only: no XNU loading, memory reservation changes or dbg_init.
 */
#include <stdint.h>
#include "cactus_trace.h"
#include "cactus_usb.h"
#include "fdt.h"
#include "fdt_ranges.h"

enum {
    ENTRY = 0x1700, USB_READY = 0x1701, HEARTBEAT = 0x1702,
    BEFORE_HEADER = 0x1710, HEADER = 0x1711, PARSED = 0x1712,
    BEFORE_BANK = 0x1713, BANK = 0x1714, RESERVED = 0x1715,
    INITRD = 0x1716, XNU_WINDOW = 0x1717, MAP_DONE = 0x1718,
    FAULT = 0x17ee, DONE = 0x17ff
};

static uint32_t diagnostics[16];
static uint64_t frequency;
static unsigned online, faulted;
static unsigned heartbeat;

static uint64_t tick(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

static void log_event(unsigned event, uint64_t a, uint64_t b, uint64_t c)
{
    (void)cactus_trace_emit(event, a, b, c);
}

static void poll_for(unsigned milliseconds)
{
    const uint64_t start = tick();
    const uint64_t duration = (frequency / 1000u) * milliseconds;
    uint64_t last = start;
    while (tick() - start < duration) {
        if (online) {
            usb_poll();
            if (usb_online() && (diagnostics[8] & 1u)) {
                const uint8_t *frame = cactus_trace_peek();
                if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
                    cactus_trace_pop();
            }
        }
        const uint64_t now = tick();
        if (now - last >= frequency) {
            last = now;
            log_event(HEARTBEAT, heartbeat++, diagnostics[8],
                      ((uint64_t)cactus_trace_dropped() << 32) |
                      cactus_trace_pending());
        }
    }
}

void stage17_probe(uintptr_t dtb)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    if (!frequency) frequency = 13000000u;
    online = faulted = heartbeat = 0;
    cactus_trace_reset(0xca170001u);
    log_event(ENTRY, dtb, el, frequency);
    online = usb_init(diagnostics);
    log_event(USB_READY, online, diagnostics[1], diagnostics[2]);
    if (!online) return;

    /* Do these reads immediately. The ring queues them until CDC DTR
     * arrives, even if the device will reset before ten seconds. */
    log_event(BEFORE_HEADER, dtb, 0, 0);
    uint32_t bytes = 0;
    const int header_ok = fdt_check(dtb, &bytes);
    log_event(HEADER, (uint32_t)header_ok, bytes, dtb);

    /* Local stack storage avoids depending on probe C .bss lifetime. */
    struct fdt_info info;
    int parsed = 0;
    if (header_ok && bytes <= 0x100000u)
        parsed = (fdt_inspect((const void *)dtb, bytes, &info) == 0);
    log_event(PARSED, parsed,
              parsed ? info.memory_count : 0,
              parsed ? info.reserved_count : 0);
    if (parsed) {
        for (unsigned i = 0; i < info.memory_count; i++) {
            /* Separate the loop entry and the actual struct dereference
             * to identify the exact failing instruction class. */
            log_event(BEFORE_BANK, i, info.memory_count, 0);
            log_event(BANK, i, info.memory[i].base, info.memory[i].size);
        }
        for (unsigned i = 0; i < info.reserved_count; i++)
            log_event(RESERVED, i, info.reserved[i].base,
                      info.reserved[i].size);
        log_event(INITRD, info.initrd_start, info.initrd_end, 0);
        log_event(XNU_WINDOW,
                  fdt_region_free(&info, 0x70000000ull, 0x50000000ull),
                  0x70000000ull, 0x50000000ull);
    }
    log_event(MAP_DONE, parsed, cactus_trace_pending(),
              cactus_trace_dropped());

    /* Keep hardware and CDC activity alive to distinguish a code crash
     * during map reporting from a later reset/USB transport failure. */
    poll_for(15000);
    log_event(DONE, parsed, heartbeat, cactus_trace_dropped());
    poll_for(2000);
    usb_shutdown();
    online = 0;
}

void stage17_fault(uint64_t esr, uint64_t elr, uint64_t far)
{
    if (faulted)
        for (;;) __asm__ volatile("wfe");
    faulted = 1;
    log_event(FAULT, esr, elr, far);
    if (online) poll_for(2500);
    for (;;) __asm__ volatile("wfe");
}
