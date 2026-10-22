/* Real cactus shim, synchronous USB diagnostics. Works before the XNU MMU.
 * USB polling cannot continue after XNU takes ownership of the CPU. */
#include "stage15.h"
#include "cactus_usb.h"
#include "cactus_trace.h"
#include "watchdog.h"

static uint32_t usb_diag[16];
static unsigned started, connected, in_fault, preflight_ok;

static uint64_t ticks(void)
{
    uint64_t n;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(n));
    return n;
}

static uint64_t hz(void)
{
    uint64_t n;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(n));
    return n ? n : 13000000u;
}

/* Bounded and allocation-free; no retries after a bulk FIFO write. */
static void drain_ms(unsigned ms)
{
    if (!connected) return;
    const uint64_t start = ticks(), duration = hz() / 1000u * ms;
    while (ticks() - start < duration) {
        usb_poll();
        const uint8_t *frame = cactus_trace_peek();
        if (frame && usb_send_budgeted(frame, CACTUS_TRACE_BYTES) == 1)
            cactus_trace_pop();
        wdt_ping();
    }
}

void stage15_mark(uint32_t event, uint64_t a, uint64_t b, uint64_t c)
{
    if (!started) return;
    if (event == S15_LAYOUT_OK) preflight_ok = 1;
    if (event == S15_ERROR) preflight_ok = 0;
    cactus_trace_emit(event, a, b, c);
    drain_ms(80);
}

unsigned stage15_preflight_ok(void) { return preflight_ok; }

void stage15_begin(uintptr_t dtb)
{
    cactus_trace_reset(0xca150001u);
    started = 1;
    preflight_ok = in_fault = connected = 0;
    cactus_trace_emit(S15_ENTRY, dtb, (uintptr_t)stage15_begin, 0);
    connected = usb_init(usb_diag);
    if (!connected) return;
    const uint64_t start = ticks(), timeout = hz() * 6u;
    while (ticks() - start < timeout) {
        usb_poll();
        /* Preserve earliest boot records until the Windows CDC reader
         * opens COM and asserts DTR, so EP1 writes are not discarded. */
        if (usb_online() && (usb_diag[8] & 1u)) break;
        wdt_ping();
    }
    stage15_mark(S15_USB_READY, usb_online(), usb_diag[8], usb_diag[2]);
}

void stage15_fault(uint64_t slot, uint64_t esr, uint64_t elr,
                   uint64_t far, uint64_t spsr)
{
    if (!started || in_fault) return;
    in_fault = 1;
    /* First record captures fault address + program counter + ESR;
     * second records the saved process state and vector slot. */
    cactus_trace_emit(S15_FAULT, elr, esr, far);
    cactus_trace_emit(S15_ERROR, slot, spsr, 0);
    drain_ms(450);
}

void stage15_end(uint32_t reason)
{
    if (!started) return;
    cactus_trace_emit(S15_DIAG_STOP, reason,
                      cactus_trace_dropped(), cactus_trace_pending());
    drain_ms(1600);
    usb_shutdown();
    connected = started = 0;
}

void stage15_el1_ready(uintptr_t entry, uintptr_t boot_args)
{
    /* Executed by shim in EL1h, after ERET; XNU has not executed yet. */
    stage15_mark(S15_EL1_READY, entry, boot_args, 0);
    stage15_mark(S15_XNU_BRANCH, entry, boot_args, 0);
    drain_ms(550);
}
