/*
 * Cactus Stage 15 EARLY: exercise the *actual* XNU shim Linux Image
 * entry/stack/BSS with exactly the hardware-positive Stage 14 MUSB driver.
 * Deliberately bypass shim_main and every normal debug/display/WDT access.
 *
 * Build STAGE15_EARLY=1: USB + changing records only
 *       STAGE15_EARLY=2: then original dbg_init / reserved RAM
 *       STAGE15_EARLY=3: then watchdog init and ping
 *       STAGE15_EARLY=4: then inspect LK's live FDT, never copy XNU
 *
 * Records are proof of execution at each marker, not XNU-origin logging.
 * No Linux chainload, no bootloader writes, no kernel relocation.
 */
#include <stdint.h>
#include "stage15_early.h"
#include "cactus_usb.h"
#include "cactus_trace.h"
#if SHIM_STAGE15_EARLY >= 2
#include "debug.h"
#endif
#if SHIM_STAGE15_EARLY >= 3
#include "watchdog.h"
#endif
#if SHIM_STAGE15_EARLY >= 4
#include "fdt.h"
#include "fdt_ranges.h"
#endif

#if !defined(BOARD_CACTUS) || !defined(SHIM_STAGE15_EARLY) || SHIM_STAGE15_EARLY < 1
#error Stage 15 Early is cactus-only and requires STAGE15_EARLY=1..4
#endif

enum {
    EVT_ENTRY       = 0x1580,
    EVT_USB_INIT    = 0x1581,
    EVT_HEARTBEAT   = 0x1582,
    EVT_DBG_BEFORE  = 0x1583,
    EVT_DBG_AFTER   = 0x1584,
    EVT_WDT_BEFORE  = 0x1585,
    EVT_WDT_AFTER   = 0x1586,
    EVT_FDT_BEFORE  = 0x1587,
    EVT_FDT_AFTER   = 0x1588,
    EVT_DRAM_BANK   = 0x1589,
    EVT_DONE        = 0x158a,
    EVT_EXCEPTION   = 0x15ef,
    EVT_EXC_STATE   = 0x15ee,
};

static uint32_t diagnostics[16];
static unsigned connected, in_fault, running;
static uint32_t heartbeat;
static uint64_t ticks_per_second;

static uint64_t now(void)
{
    uint64_t t;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
    return t;
}

static void drain(void)
{
    if (!connected) return;
    usb_poll();
    /* Keep first entry record in RAM until a Windows CDC client opens. */
    if (!usb_online() || !(diagnostics[8] & 1u)) return;
    const uint8_t *frame = cactus_trace_peek();
    if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
        cactus_trace_pop();
}

static void record(uint32_t event, uint64_t a, uint64_t b, uint64_t c)
{
    (void)cactus_trace_emit(event, a, b, c);
}

static void pump(unsigned ms)
{
    const uint64_t started = now();
    const uint64_t duration = (ticks_per_second / 1000u) * ms;
    uint64_t last_heartbeat = started;
    while (now() - started < duration) {
        drain();
        uint64_t t = now();
        if (t - last_heartbeat >= ticks_per_second / 4u) {
            last_heartbeat = t;
            record(EVT_HEARTBEAT, heartbeat++, diagnostics[8],
                   ((uint64_t)cactus_trace_dropped() << 32) |
                   cactus_trace_pending());
        }
#if SHIM_STAGE15_EARLY >= 3
        if (running >= 3) wdt_ping();
#endif
    }
}

void stage15_early_main(uintptr_t dtb)
{
    uint64_t el, mpidr;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(ticks_per_second));
    if (!ticks_per_second) ticks_per_second = 13000000u;
    cactus_trace_reset(0xca15e000u + SHIM_STAGE15_EARLY);
    heartbeat = in_fault = connected = running = 0;
    record(EVT_ENTRY, dtb, el, mpidr);
    connected = usb_init(diagnostics);
    record(EVT_USB_INIT, connected, diagnostics[1], diagnostics[2]);
    if (!connected) return;
    running = 1;

    /* Give Windows time to discover VID_1209/PID_0001 and assert DTR. */
    pump(8000);
#if SHIM_STAGE15_EARLY >= 2
    record(EVT_DBG_BEFORE, 0, 0, 0);
    pump(1250);
    dbg_init();
    record(EVT_DBG_AFTER, 0, 0, 0);
    pump(1250);
#endif
#if SHIM_STAGE15_EARLY >= 3
    record(EVT_WDT_BEFORE, 20, 0, 0);
    pump(1250);
    wdt_init(20);
    running = 3;
    record(EVT_WDT_AFTER, wdt_mode(), wdt_timeout(), 0);
    pump(1250);
#endif
#if SHIM_STAGE15_EARLY >= 4
    record(EVT_FDT_BEFORE, dtb, 0, 0);
    pump(1250);
    uint32_t size = 0;
    struct fdt_info info;
    int header_ok = fdt_check(dtb, &size);
    int parse_result = header_ok ?
        fdt_inspect((const void *)dtb, size, &info) : -1;
    record(EVT_FDT_AFTER, (uint32_t)parse_result,
           header_ok ? size : 0, parse_result ? 0 : info.memory_count);
    if (!parse_result) {
        for (unsigned i = 0; i < info.memory_count; ++i)
            record(EVT_DRAM_BANK, i, info.memory[i].base, info.memory[i].size);
    }
    pump(1250);
#endif
    pump(9000);
    record(EVT_DONE, SHIM_STAGE15_EARLY, heartbeat,
           ((uint64_t)cactus_trace_dropped() << 32) |
           cactus_trace_pending());
    pump(1250);
#if SHIM_STAGE15_EARLY >= 3
    wdt_disable();
#endif
    usb_shutdown();
    running = connected = 0;
}

void stage15_early_fault(uint64_t vector, uint64_t esr, uint64_t elr,
                         uint64_t far, uint64_t spsr)
{
    if (!in_fault) {
        in_fault = 1;
        record(EVT_EXCEPTION, vector, esr, elr);
        record(EVT_EXC_STATE, far, spsr, running);
        /* If the exception is due to MMIO, the recorder itself may
         * also trap; the recursion guard then halts the CPU safely. */
        if (connected) pump(5000);
    }
    for (;;)
        __asm__ volatile("wfe");
}
