/*
 * Persistent breadcrumbs for the CACTUS shim.
 *
 * The pstore region is a convenience, not a requirement: if the region is
 * unavailable the shim still runs and the watchdog/panic screen is the
 * primary evidence.  Writes are cache-cleaned so a reset does not lose them.
 */
#include "debug.h"
#include "board.h"
#include "pmsg_trace.h"

#ifndef CACTUS_PMSG_TRACE
#define CACTUS_PMSG_TRACE 0
#endif

_Static_assert(sizeof(struct dbg_entry) == 56, "dbg entry layout");

#if BOARD_HAS_DBG

#define DBG_HEADER_SIZE		((uint32_t)sizeof(struct dbg_header))
#define DBG_RING_SIZE		(DBG_TRACE_COUNT * (uint32_t)sizeof(struct dbg_entry))

#if !(defined(BOARD_CACTUS) && CACTUS_PMSG_TRACE)
static inline uint64_t dbg_now(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static inline uint64_t dbg_cpu(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
	return v & 0xffffff;
}

static void copy_name(volatile char *dst, const char *src)
{
	unsigned i;

	for (i = 0; i + 1 < DBG_NAME_LEN && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

static void dbg_clean(uintptr_t addr, uint32_t len)
{
	uintptr_t p = addr & ~63UL;
	uintptr_t end = addr + len;

	while (p < end) {
		__asm__ volatile("dc cvac, %0" : : "r"(p) : "memory");
		p += 64;
	}
	__asm__ volatile("dsb sy; isb" ::: "memory");
}

static volatile struct dbg_header *hdr(void)
{
	return (volatile struct dbg_header *)DBG_BASE;
}
#endif

void dbg_init(void)
{
#if defined(BOARD_CACTUS) && CACTUS_PMSG_TRACE
	/* Native pmsg is the only persistent sink in a trace build. The old
	 * CDB1 buffer overlaps recovery's ramoops console area. */
	pmsg_trace_mark("ENTRY", 0, 0, 0);
#else
	volatile struct dbg_header *h = hdr();
	uint64_t t = dbg_now();

	h->magic = DBG_MAGIC;
	h->version = DBG_VERSION;
	h->seq = 0;
	h->count = 0;
	h->entry_ticks = t;
	h->last_ticks = t;
	h->cpu = (uint32_t)dbg_cpu();
	h->flags = 0;
	copy_name(h->last, "ENTRY");
	h->a = 0;
	h->b = 0;
	h->c = 0;
	h->reserved0 = 0;
	h->reserved1 = 0;
	dbg_clean(DBG_BASE, DBG_HEADER_SIZE);
#endif
}

void dbg_mark(const char *name, uint64_t a, uint64_t b, uint64_t c)
{
#if defined(BOARD_CACTUS) && CACTUS_PMSG_TRACE
	pmsg_trace_mark(name, a, b, c);
#else
	volatile struct dbg_header *h = hdr();
	volatile struct dbg_entry *e;
	uint32_t seq;

	if (h->magic != DBG_MAGIC)
		return;

	seq = ++h->seq;
	e = (volatile struct dbg_entry *)(DBG_BASE + DBG_TRACE_OFF) +
	    ((seq - 1) % DBG_TRACE_COUNT);

	e->seq = seq;
	e->cpu = (uint32_t)dbg_cpu();
	e->ticks = dbg_now();
	copy_name(e->name, name);
	e->a = a;
	e->b = b;
	e->c = c;

	h->count = seq < DBG_TRACE_COUNT ? seq : DBG_TRACE_COUNT;
	h->last_ticks = e->ticks;
	h->cpu = e->cpu;
	copy_name(h->last, name);
	h->a = a;
	h->b = b;
	h->c = c;

	dbg_clean((uintptr_t)e, sizeof(*e));
	dbg_clean(DBG_BASE, DBG_HEADER_SIZE);
#endif
}

void dbg_stage(const char *name)
{
	dbg_mark(name, 0, 0, 0);
}

void dbg_flush(void)
{
#if !(defined(BOARD_CACTUS) && CACTUS_PMSG_TRACE)
	if (hdr()->magic != DBG_MAGIC)
		return;
	dbg_clean(DBG_BASE, DBG_HEADER_SIZE + DBG_RING_SIZE);
#endif
}

#else

void dbg_init(void) {}
void dbg_stage(const char *name) { (void)name; }
void dbg_mark(const char *name, uint64_t a, uint64_t b, uint64_t c)
{
	(void)name;
	(void)a;
	(void)b;
	(void)c;
}
void dbg_flush(void) {}

#endif
