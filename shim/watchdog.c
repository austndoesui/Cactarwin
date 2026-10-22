/*
 * MT6765 top RGU watchdog + software progress deadman.
 *
 * The hardware watchdog is the last-resort recovery path when the shim hangs
 * with interrupts masked.  It is disarmed immediately before jumping to XNU
 * because XNU does not service the MTK watchdog.
 */
#include "watchdog.h"
#include "board.h"
#include "bootmode.h"
#include "debug.h"

#define WDT_MODE		0x00
#define WDT_LENGTH		0x04
#define WDT_RST			0x08
#define WDT_SWRST		0x14

#define WDT_MODE_EN		(1U << 0)
#define WDT_MODE_EXRST_EN	(1U << 2)
#define WDT_MODE_DDR_RSVD	(1U << 7)
#define WDT_MODE_KEY		0x22000000U
#define WDT_LENGTH_KEY		0x8U
#define WDT_RST_RELOAD		0x1971U
#define WDT_SWRST_KEY		0x1209U

#if BOARD_HAS_WDT

static volatile uint64_t last_ticks;
static volatile uint64_t timeout_ticks;
static volatile unsigned timeout_s;
static volatile int ready;

static inline uint32_t rd32(uintptr_t a)
{
	return *(volatile uint32_t *)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static inline uint64_t now(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static inline uint64_t freq(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static int program(unsigned seconds)
{
	uint32_t mode = rd32(WDT_BASE + WDT_MODE);

	if (mode == 0xffffffffU)
		return 0;

	wr32(WDT_BASE + WDT_LENGTH, (seconds << 11) | WDT_LENGTH_KEY);
	wr32(WDT_BASE + WDT_RST, WDT_RST_RELOAD);
	mode |= WDT_MODE_KEY | WDT_MODE_EN | WDT_MODE_EXRST_EN | WDT_MODE_DDR_RSVD;
	wr32(WDT_BASE + WDT_MODE, mode);
	__asm__ volatile("dsb sy" ::: "memory");

	return (rd32(WDT_BASE + WDT_MODE) & WDT_MODE_EN) != 0;
}

void wdt_early_init(void)
{
	/* No globals here: this runs before .bss is cleared. */
	(void)program(30);
}

void wdt_init(unsigned seconds)
{
	ready = program(seconds);
	timeout_s = seconds;
	timeout_ticks = (uint64_t)seconds * freq();
	last_ticks = now();
	if (ready)
		wr32(WDT_BASE + WDT_RST, WDT_RST_RELOAD);
}

void wdt_ping(void)
{
	if (!ready)
		return;
	wr32(WDT_BASE + WDT_RST, WDT_RST_RELOAD);
	last_ticks = now();
}

void wdt_check(void)
{
	uint64_t f;

	if (!ready)
		return;
	f = freq();
	timeout_ticks = (uint64_t)timeout_s * f;
	if (f && now() - last_ticks > timeout_ticks) {
		dbg_stage("WDT DEADMAN");
		dbg_flush();
		wdt_sw_reset();
	}
}

void wdt_disable(void)
{
	uint32_t mode;

	if (!ready)
		return;
	mode = rd32(WDT_BASE + WDT_MODE);
	mode &= ~WDT_MODE_EN;
	mode |= WDT_MODE_KEY;
	wr32(WDT_BASE + WDT_MODE, mode);
	__asm__ volatile("dsb sy" ::: "memory");
	ready = 0;
}

void wdt_sw_reset(void)
{
	wdt_ping();
	bootmode_prepare_reset();
	wr32(WDT_BASE + WDT_SWRST, WDT_SWRST_KEY);
	__asm__ volatile("dsb sy" ::: "memory");
	for (;;)
		__asm__ volatile("wfe");
}

uint32_t wdt_mode(void)
{
	return rd32(WDT_BASE + WDT_MODE);
}

unsigned wdt_timeout(void)
{
	return timeout_s;
}

#else

void wdt_early_init(void) {}
void wdt_init(unsigned seconds) { (void)seconds; }
void wdt_ping(void) {}
void wdt_check(void) {}
void wdt_disable(void) {}
void wdt_sw_reset(void)
{
	for (;;)
		__asm__ volatile("wfe");
}
uint32_t wdt_mode(void) { return 0; }
unsigned wdt_timeout(void) { return 0; }

#endif
