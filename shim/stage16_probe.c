/*
 * Stage 16: execute genuine cactus shim FDT and reserved-RAM debug
 * routines inside the hardware-proven Stage 14 Linux/TWRP wrapper.
 *
 * USB remains polled at EL2 with the MMU off. We do NOT relocate,
 * copy or enter XNU until the live memory map has been verified.
 */
#include <stdint.h>
#include "cactus_usb.h"
#include "cactus_trace.h"
#include "debug.h"
#include "fdt.h"
#include "fdt_ranges.h"

enum {
    S16_ENTRY         = 0x1600,
    S16_USB_READY     = 0x1601,
    S16_HEARTBEAT     = 0x1602,
    S16_FDT_BEFORE    = 0x1610,
    S16_FDT_HEADER    = 0x1611,
    S16_FDT_PARSED    = 0x1612,
    S16_RAM_BANK      = 0x1613,
    S16_RESERVED      = 0x1614,
    S16_INITRD        = 0x1615,
    S16_XNU_RAM_CHECK = 0x1616,
    S16_DEBUG_BEFORE  = 0x1620,
    S16_DEBUG_AFTER   = 0x1621,
    S16_DONE          = 0x16ff,
    S16_EXCEPTION     = 0x16ee,
};

static uint32_t usb_diag[16];
static struct fdt_info live_fdt;
static uint64_t counter_hz;
static uint32_t heartbeats;
static unsigned connected, in_fault;

static uint64_t ticks(void)
{
    uint64_t t;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static void emit(uint32_t event, uint64_t a, uint64_t b, uint64_t c)
{
    cactus_trace_emit(event, a, b, c);
}

static void pump(unsigned milliseconds)
{
    const uint64_t start = ticks();
    const uint64_t duration = (counter_hz / 1000u) * milliseconds;
    uint64_t last_beat = start;
    while (ticks() - start < duration) {
        if (connected) {
            usb_poll();
            /* Preserve the early entries until the Windows host opens CDC. */
            if (usb_online() && (usb_diag[8] & 1u)) {
                const uint8_t *frame = cactus_trace_peek();
                if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
                    cactus_trace_pop();
            }
        }
        const uint64_t t = ticks();
        if (t - last_beat >= counter_hz / 2u) {
            last_beat = t;
            emit(S16_HEARTBEAT, heartbeats++, usb_diag[8],
                 ((uint64_t)cactus_trace_dropped() << 32) |
                 cactus_trace_pending());
        }
    }
}

void stage16_probe(uintptr_t dtb)
{
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_hz));
    if (!counter_hz) counter_hz = 13000000u;
    cactus_trace_reset(0xca160001u);
    heartbeats = connected = in_fault = 0;
    emit(S16_ENTRY, dtb, current_el, counter_hz);
    connected = usb_init(usb_diag);
    emit(S16_USB_READY, connected, usb_diag[1], usb_diag[2]);
    if (!connected) return;

    /* The actual Stage 15 hybrid needed several seconds to open COM25. */
    pump(7000);
    emit(S16_FDT_BEFORE, dtb, 0, 0);
    pump(800);

    uint32_t fdt_bytes = 0;
    int header_ok = fdt_check(dtb, &fdt_bytes);
    emit(S16_FDT_HEADER, (uint32_t)header_ok, fdt_bytes, dtb);
    pump(700);

    int parse_ok = 0;
    if (header_ok && fdt_bytes <= 0x100000u)
        parse_ok = !fdt_inspect((const void *)dtb, fdt_bytes, &live_fdt);
    emit(S16_FDT_PARSED, (uint32_t)parse_ok,
         parse_ok ? live_fdt.memory_count : 0,
         parse_ok ? live_fdt.reserved_count : 0);
    pump(650);
    if (parse_ok) {
        for (unsigned i = 0; i < live_fdt.memory_count; ++i) {
            emit(S16_RAM_BANK, i, live_fdt.memory[i].base,
                 live_fdt.memory[i].size);
            pump(140);
        }
        /* Record only the first eight reserved ranges; the total
         * count is in S16_FDT_PARSED and the ring holds 32 frames. */
        const unsigned count = live_fdt.reserved_count < 8u ?
                               live_fdt.reserved_count : 8u;
        for (unsigned i = 0; i < count; ++i) {
            emit(S16_RESERVED, i, live_fdt.reserved[i].base,
                 live_fdt.reserved[i].size);
            pump(140);
        }
        emit(S16_INITRD, live_fdt.initrd_start, live_fdt.initrd_end, 0);
        /* Existing XNU payload requests [0x70000000, 0xc0000000).
         * Log the verdict only; no RAM writes or boot_args creation. */
        emit(S16_XNU_RAM_CHECK,
             fdt_region_free(&live_fdt, 0x70000000u, 0x50000000u),
             0x70000000u, 0x50000000u);
        pump(600);
    }

    emit(S16_DEBUG_BEFORE, DBG_BASE, 0, 0);
    pump(900);
    /* The same dbg_init used by shim_main. This stage is its only
     * intentional reserved-RAM write and runs after FDT inspection. */
    dbg_init();
    emit(S16_DEBUG_AFTER, DBG_BASE, DBG_MAGIC, 0);
    pump(1250);

    emit(S16_DONE, parse_ok, heartbeats,
         ((uint64_t)cactus_trace_dropped() << 32) |
         cactus_trace_pending());
    pump(2000);
    usb_shutdown();
    connected = 0;
    /* The assembly wrapper resumes its verified recovery chainload. */
}

void stage16_fault(uint64_t syndrome, uint64_t address, uint64_t fault_va)
{
    if (in_fault)
        for (;;) __asm__ volatile("wfe");
    in_fault = 1;
    emit(S16_EXCEPTION, syndrome, address, fault_va);
    if (connected) pump(2500);
    for (;;) __asm__ volatile("wfe");
}
