/*
 * Early boot shim for the XNU payload.
 *
 * The bootloader enters this as an arm64 Image with x0 = DTB, MMU and caches
 * off, EL1.  If the ramdisk contains a SWEETXNU payload we load it and jump
 * to XNU.  Otherwise we print as much early hardware/FDT state as possible
 * and stop, so a failed device boot is diagnosable without UART.
 */
#include <stdint.h>
#include "console.h"
#include "debug.h"
#include "fdt.h"
#include "mmu.h"
#include "loader.h"
#include "watchdog.h"
#include "bootmode.h"
#if defined(SHIM_STAGE15) && SHIM_STAGE15
#include "stage15.h"
#endif
#if defined(SHIM_STAGE15_EARLY) && SHIM_STAGE15_EARLY
#include "stage15_early.h"
#endif
#if BOARD_HAS_UFS
#include "ufs.h"
#endif

#if !defined(BOARD_CACTUS)
#define IMEM_RESTART_REASON	0x146aa65cUL	/* qcom,msm-imem restart_reason@65c */
#define RESTART_BOOTLOADER	0x77665500U
#define PSHOLD			0x0c264000UL
#endif

#define PSCI_VERSION		0x84000000UL
#define PSCI_CPU_SUSPEND	0xc4000001UL
#define PSCI_CPU_OFF		0x84000002UL
#define PSCI_CPU_ON		0xc4000003UL
#define PSCI_MIGRATE_INFO_TYPE	0xc4000006UL
#define PSCI_SYSTEM_RESET	0x84000009UL
#define PSCI_FEATURES		0x8400000aUL
#define PSCI_SYSTEM_SUSPEND	0xc400000eUL
#define PSCI_SYSTEM_RESET2	0xc4000012UL
#define SMCCC_VERSION		0x80000000UL

#ifndef SHIM_RESET_ONLY
#define SHIM_RESET_ONLY		0
#endif
#ifndef SHIM_RTC_PROBE
#define SHIM_RTC_PROBE		0
#endif
#ifndef SHIM_ENTRY_PROBE
#define SHIM_ENTRY_PROBE		0
#endif

#define NR_CPUS			8
#define WDT_TIMEOUT_SECONDS	20
#define PANIC_HOLD_SECONDS	10
#define DIAG_HOLD_SECONDS	5

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint64_t rd64(uintptr_t a) { return *(volatile uint64_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

extern char secondary_entry[];

static const char *stage;
static volatile uint64_t dbg_step;
static volatile uint64_t sec_mpidr[NR_CPUS];
static volatile uint64_t sec_probe[4];
static volatile uint32_t irq_count, irq_other, irq_last;
static volatile int irq_src;

static void mark(const char *s)
{
	stage = s;
	dbg_step++;
	dbg_stage(s);
	wdt_ping();
}

/* ICC_SGI1R_EL1 value targeting the calling core only. */
static uint64_t sgi_self(unsigned id)
{
	uint64_t m = rsr("mpidr_el1");
	return ((m >> 16) & 0xff) << 32 |	/* Aff2 */
	       ((m >> 8) & 0xff) << 16 |	/* Aff1 */
	       (uint64_t)id << 24 |
	       (1U << (m & 0xf));		/* target list: Aff0 */
}

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17", "memory");
	return x0;
}

static uint64_t now(void)
{
	isb();
	return rsr("cntpct_el0");
}

static void delay_ms(unsigned ms)
{
	uint64_t f = rsr("cntfrq_el0");
	if (!f || !ms)
		return;
	uint64_t end = now() + f / 1000 * ms;
	while (now() < end) {
		wdt_ping();
		wdt_check();
	}
}

static void __attribute__((noreturn)) reset(unsigned after_s)
{
	mark("reset");
	con_skip();
	con_str("RESET", "hold VOL DOWN");
	if (after_s) {
		con_hex("HOLD", after_s);
		delay_ms(after_s * 1000);
	}

	bootmode_prepare_reset();
	dbg_stage("reset now");
	dbg_flush();
	__asm__ volatile("dsb sy" ::: "memory");

#if !defined(BOARD_CACTUS)
	wr32(IMEM_RESTART_REASON, RESTART_BOOTLOADER);
	__asm__ volatile("dsb sy" ::: "memory");
#endif
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
#if !defined(BOARD_CACTUS)
	wr32(PSHOLD, 0);
#endif

	/* PSCI returned or failed: force a top RGU reset. */
	wdt_sw_reset();
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- exceptions ---- */

static void irq_handler(void)
{
	uint64_t iar = rsr("icc_iar1_el1");
	uint32_t id = iar & 0xffffff;

	if (id >= 1020 && id <= 1023)
		return;	/* spurious */
	irq_last = id;
	if (id == (uint32_t)irq_src)
		irq_count++;
	else
		irq_other++;
	if (id == VTIMER_INTID)
		wsr("cntv_tval_el0", rsr("cntfrq_el0") / 100);	/* 10 ms */
	else if (id == PTIMER_INTID)
		wsr("cntp_tval_el0", rsr("cntfrq_el0") / 100);
	wsr("icc_eoir1_el1", iar);
	isb();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	uint64_t el = rsr("CurrentEL");
	if (slot == 5 && el == 4) {	/* GIC EL1 IRQ from current EL, SPx */
		irq_handler();
		return;
	}

	uint64_t esr, elr, far, spsr;
	if (el == 8) {
		esr = rsr("esr_el2");
		elr = rsr("elr_el2");
		far = rsr("far_el2");
		spsr = rsr("spsr_el2");
	} else {
		esr = rsr("esr_el1");
		elr = rsr("elr_el1");
		far = rsr("far_el1");
		spsr = rsr("spsr_el1");
	}
#if defined(SHIM_STAGE15_EARLY) && SHIM_STAGE15_EARLY
	stage15_early_fault(slot, esr, elr, far, spsr);
#endif
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_fault(slot, esr, elr, far, spsr);
#endif
	/* Preserve the fault cause before console or watchdog access can hang. */
	dbg_mark("fault regs", slot, esr, elr);
	dbg_mark("fault addr", far, spsr, regs[30]);
	mark("exception");

	con_clear(BLUE);
	switch (slot & 3) {
	case 0: con_str("PANIC", "SYNC"); break;
	case 1: con_str("PANIC", "IRQ"); break;
	case 2: con_str("PANIC", "FIQ"); break;
	default: con_str("PANIC", "SERROR"); break;
	}
	con_hex2("SLOT", "n=", slot, "cpu=", rsr("mpidr_el1"));
	con_str("STAGE", stage ? stage : "?");
	con_hex("STEP", dbg_step);
	con_hex("ESR", esr);
	con_hex2("EC", "ec=", esr >> 26, "iss=", esr & 0x1ffffff);
	con_hex2("FSC", "fsc=", esr & 0x3f, "wnr=", (esr >> 6) & 1);
	con_hex("ELR", elr);
	con_hex("FAR", far);
	con_hex("SPSR", spsr);
	con_hex("CUR", rsr("CurrentEL"));
	if (el == 8) {
		con_hex("SCTLR", rsr("sctlr_el2"));
		con_hex("TTBR0", rsr("ttbr0_el2"));
		con_hex("TCR", rsr("tcr_el2"));
	} else {
		con_hex("SCTLR", rsr("sctlr_el1"));
		con_hex("TTBR0", rsr("ttbr0_el1"));
		con_hex("TCR", rsr("tcr_el1"));
	}
	con_hex("X0", regs[0]);
	con_hex("X1", regs[1]);
	con_hex("X2", regs[2]);
	con_hex("X29", regs[29]);
	con_hex("X30", regs[30]);
	con_hex("WDT", wdt_mode());
	con_hex("WDTO", wdt_timeout());
	dbg_stage("panic");
	dbg_flush();
	reset(PANIC_HOLD_SECONDS);
}

/* ---- SMP ---- */

static uintptr_t find_gicr(void);

void secondary_main(uint64_t idx)
{
	sec_mpidr[idx] = rsr("mpidr_el1") | (1ULL << 63);
	if (idx == 1) {
		uintptr_t r = find_gicr();
		if (r) {
			sec_probe[0] = rd32(r + GICR_IGROUPR0);
			wr32(r + GICR_IGROUPR0, 0xffffffff);
			sec_probe[1] = rd32(r + GICR_IGROUPR0);
			wr8(r + GICR_IPRIORITYR + VTIMER_INTID, 0xa0);
			sec_probe[2] = rd32(r + GICR_IPRIORITYR + 16);
		}
		sec_probe[3] = r ? 1 : 0xdead;
	}
	__asm__ volatile("dsb sy" ::: "memory");
	smc(PSCI_CPU_OFF, 0, 0, 0);
}

static void __attribute__((unused)) test_smp(void)
{
	mark("smp");
	uint64_t self = rsr("mpidr_el1") & 0xffffff;
	int64_t ret[NR_CPUS];

	for (int i = 1; i < NR_CPUS; i++)
		ret[i] = smc(PSCI_CPU_ON,
#if defined(BOARD_CACTUS)
			((uint64_t)(i / 4) << 8) | (i % 4),
#else
			(uint64_t)i << 8,
#endif
			(uintptr_t)secondary_entry, i);
	delay_ms(500);
	for (int i = 1; i < NR_CPUS; i++) {
		char label[] = "CPU0";
		label[3] = '0' + i;
		uint64_t target =
#if defined(BOARD_CACTUS)
			((uint64_t)(i / 4) << 8) | (i % 4);
#else
			(uint64_t)i << 8;
#endif
		if (target == self)
			con_str(label, "is boot cpu");
		else
			con_hex2(label, "ret=", (uint32_t)ret[i], "mpidr=",
				 (uint32_t)sec_mpidr[i] | (sec_mpidr[i] ? 0 : 0xdead0000));
	}
}

/* ---- GIC + timer ---- */

static uintptr_t find_gicr(void)
{
	uint64_t aff = rsr("mpidr_el1");
	uint32_t want = (aff & 0xffffff) | ((aff >> 8) & 0xff000000);

	for (uintptr_t off = 0; off < GICR_SIZE; off += GICR_PE_SIZE) {
		uint64_t typer = rd64(GICR_BASE + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want)
			return GICR_BASE + off;
		if (typer & (1 << 4))	/* Last */
			break;
	}
	return 0;
}

static void gicd_wait_rwp(void)
{
	for (int i = 0; i < 1000000 && (rd32(GICD_BASE) & (1U << 31)); i++)
		;
}

/* Run one interrupt source for ~300 ms and report what the GIC saw. */
static void irq_window(const char *label, uintptr_t r, int src)
{
	uint64_t f = rsr("cntfrq_el0");
	uint32_t pend = 0, act = 0, isr = 0, hppi = 0x3ff, ist = 0;

	if (!f)
		return;
	irq_count = irq_other = 0;
	irq_last = 0x3ff;
	irq_src = src;
	if (src == VTIMER_INTID) {
		wsr("cntv_tval_el0", f / 100);
		wsr("cntv_ctl_el0", 1);
	} else if (src == PTIMER_INTID) {
		wsr("cntp_tval_el0", f / 100);
		wsr("cntp_ctl_el0", 1);
	}
	isb();
	uint64_t t = now() + f / 50;
	while (now() < t)
		;
	if (src == SGI_INTID)
		wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
	isb();
	ist = src == PTIMER_INTID ? rsr("cntp_ctl_el0") : rsr("cntv_ctl_el0");
	pend = rd32(r + GICR_ISPENDR0);
	act = rd32(r + GICR_ISACTIVER0);
	isr = rsr("isr_el1");
	hppi = rsr("icc_hppir1_el1");

	__asm__ volatile("msr daifclr, #2" ::: "memory");
	t = now() + f * 3 / 10;
	uint64_t next = now() + f / 100;
	for (int sent = 1; now() < t;)
		if (src == SGI_INTID && sent < 10 && now() >= next) {
			wsr("icc_sgi1r_el1", sgi_self(SGI_INTID));
			isb();
			sent++;
			next += f / 100;
		}
	__asm__ volatile("msr daifset, #2" ::: "memory");
	wsr("cntv_ctl_el0", 0);
	wsr("cntp_ctl_el0", 0);
	isb();

	con_hex2(label, "irqs=", irq_count, "other=", irq_other);
	con_hex2(" pre", "pend=", pend, "act=", act);
	con_hex2(" pre", "isr=", isr, "hppi=", hppi);
	con_hex2(" pre", "tctl=", ist, "last=", irq_last);
}

static void __attribute__((unused)) test_gic(void)
{
	mark("gic sre");
	uint64_t sre = rsr("icc_sre_el1");
	if (!(sre & 1)) {
		wsr("icc_sre_el1", sre | 1);
		isb();
	}
	if (!(rsr("icc_sre_el1") & 1)) {
		con_str("GIC", "no sysreg iface, skip");
		return;
	}

	mark("gic dist");
	uint32_t dctlr = rd32(GICD_BASE);
	wr32(GICD_BASE, dctlr | 0x13);
	gicd_wait_rwp();
	con_hex2("GICD", "was=", dctlr, "now=", rd32(GICD_BASE));

	mark("gic redist");
	uintptr_t r = find_gicr();
	if (!r) {
		con_str("GICR", "not found");
		return;
	}
	uint32_t waker = rd32(r + GICR_WAKER);
	if (waker & 2) {
		wr32(r + GICR_WAKER, waker & ~2U);
		for (int i = 0; i < 100000 && (rd32(r + GICR_WAKER) & 4); i++)
			;
	}
	con_hex2("GICR", "base=", r, "waker=", rd32(r + GICR_WAKER));

	mark("gic config");
	uint32_t grp = rd32(r + GICR_IGROUPR0);
	wr32(r + GICR_IGROUPR0, 0xffffffff);
	uint32_t en_before = rd32(r + GICR_ISENABLER0);
	uint32_t pri_before = rd32(r + GICR_IPRIORITYR + 16);
	for (int id = 0; id < 32; id++)
		wr8(r + GICR_IPRIORITYR + id, 0xa0);
	wr32(r + GICR_ISENABLER0, (1U << VTIMER_INTID) | (1U << PTIMER_INTID) | (1U << SGI_INTID));
	con_hex2("ENABLE", "was=", en_before, "now=", rd32(r + GICR_ISENABLER0));
	(void)grp;
	(void)pri_before;

	uint64_t ctlr = rsr("icc_ctlr_el1");
	wsr("icc_ctlr_el1", ctlr & ~2ULL);
	wsr("icc_pmr_el1", 0xf0);
	wsr("icc_bpr1_el1", 0);
	wsr("icc_igrpen1_el1", 1);
	isb();
	con_hex2("ICC", "ctlr=", ctlr, "grpen1=", rsr("icc_igrpen1_el1"));

	mark("gic vtimer");
	irq_window("VTIMER", r, VTIMER_INTID);
	mark("gic sgi");
	irq_window("SGI1", r, SGI_INTID);
	wr32(r + GICR_ICENABLER0, 0xffffffff);
	isb();
}

/* ---- main ---- */

static void print_cpu(uint64_t dtb)
{
	uint64_t mmfr0 = rsr("id_aa64mmfr0_el1");
	uint64_t pfr0 = rsr("id_aa64pfr0_el1");

	con_str("SHIM", BOARD_NAME " xnu loader");
	con_skip();
	con_hex("X0/DTB", dtb);
	con_hex2("DTB", "lo=", (uint32_t)dtb, "hi=", (uint32_t)(dtb >> 32));
	con_hex2("EL", "cur=", rsr("CurrentEL"), "daif=", rsr("daif"));
	con_hex("MIDR", rsr("midr_el1"));
	con_hex("MPIDR", rsr("mpidr_el1"));
	con_hex("CNTFRQ", rsr("cntfrq_el0"));
	con_hex("CNTPCT", rsr("cntpct_el0"));
	con_hex("MMFR0", mmfr0);
	con_hex("PFR0", pfr0);
	con_hex2("TG", "4k=", mmfr0 & 0xf, "16k=", (mmfr0 >> 20) & 0xf);
	con_hex2("FP", "el0=", pfr0 & 0xf, "fp=", (pfr0 >> 16) & 0xf);
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_hex("TTBR0", rsr("ttbr0_el1"));
	con_hex("TCR", rsr("tcr_el1"));
	con_hex("VBAR", rsr("vbar_el1"));
	con_hex("ESR", rsr("esr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex2("FB", "base=", FB_BASE, "stride=", FB_STRIDE);
	con_hex2("FB", "w=", FB_W, "h=", FB_H);
	con_hex2("CON", "scale=", CON_SCALE, "margin=", CON_MARGIN);
	con_hex2("WDT", "mode=", wdt_mode(), "timeout=", wdt_timeout());
	con_skip();
}

void shim_main(uint64_t dtb)
{
#if defined(BOARD_CACTUS) && SHIM_ENTRY_PROBE
	/* Isolated, one-shot framebuffer marker. LK may leave the panel OFF;
	 * absence of a visible marker is inconclusive. No RTC, watchdog,
	 * pstore, DT parsing, or payload accesses occur on this path. */
	volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)FB_BASE;
	for (uint32_t y = 0; y < FB_H; y++)
		for (uint32_t x = 0; x < FB_W; x++)
			fb[y * (FB_STRIDE / 4) + x] = (x < FB_W / 2) ? 0x00ff00ffU : 0x0000ffffU;
	__asm__ volatile("dsb sy" ::: "memory");
	for (;;)
		__asm__ volatile("wfe");
#endif
#if SHIM_RESET_ONLY >= 2
	/* Attempt PSCI reset with no prior peripheral access. */
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	/* Fallback only if PSCI returned: MTK watchdog MMIO. */
	wdt_sw_reset();
	for (;;)
		__asm__ volatile("wfe");
#endif

	dbg_init();
	/* A separate checkpoint catches watchdog initialization faults. */
	dbg_mark("before wdt", rsr("CurrentEL"), dtb, rsr("mpidr_el1"));
	wdt_init(WDT_TIMEOUT_SECONDS);
	dbg_stage("after wdt");

	mark("entry");
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_begin(dtb);
	stage15_mark(S15_ENTRY, rsr("CurrentEL"), dtb, rsr("mpidr_el1"));
#endif

#if SHIM_RESET_ONLY == 1
	mark("reset only");
	dbg_flush();
	delay_ms(3000);
	bootmode_prepare_reset();
	mark("reset now");
	dbg_flush();
	smc(PSCI_SYSTEM_RESET, 0, 0, 0);
	wdt_sw_reset();
	for (;;)
		__asm__ volatile("wfe");
#endif

	con_clear(RED);
	print_cpu(dtb);

#if defined(BOARD_CACTUS) && SHIM_RTC_PROBE
	uint16_t pdn1 = 0;
	int rtc_rc = bootmode_probe(&pdn1);
	con_hex2("RTC", "rc=", (uint32_t)rtc_rc, "pdn1=", pdn1);
#endif

#if defined(SHIM_SELFTEST) && SHIM_SELFTEST
	mark("selftest");
	con_str("SELFTEST", "forcing sync exception");
	__asm__ volatile("brk #0");
#endif

	mark("loader");
	const char *why = loader_boot(dtb);
#if defined(SHIM_STAGE15) && SHIM_STAGE15
#if defined(SHIM_STAGE15_DIAG) && SHIM_STAGE15_DIAG
	stage15_end(stage15_preflight_ok() ? 0 : 1);
#else
	stage15_mark(S15_ERROR, (uintptr_t)why, dtb, 0);
	stage15_end(1);
#endif
#endif
	con_str("LOADER", why);
	con_skip();

	mark("fdt");
	fdt_dump(dtb);
	con_skip();

#if BOARD_HAS_UFS
	mark("ufs");
	test_ufs();
	con_str("UFS", "probe done");
	con_skip();
#endif

#if defined(BOARD_CACTUS)
	mark("done");
	con_str("DIAG", "finished, resetting");
	con_str("RESET", "hold VOL DOWN");
	reset(DIAG_HOLD_SECONDS);
#else
	/* Sweet milestone 3 diagnostics.  Not used for the XNU loader path. */
	struct mmu_info mi;
	uint64_t fill_off = 0, mem_off = 0, fill_on = 0, mem_on = 0;

	mark("mmu");
	int rc = mmu_enable(&mi);
	if (!rc) {
		uint64_t t = now();
		con_clear(RED);
		fill_on = now() - t;
	}
	con_str("MMU", rc ? "enable failed" : "enabled");
	con_hex2("IMAGE", "lo=", mi.img_lo, "hi=", mi.img_hi);
	con_hex2("MMU", "rc=", rc, "l3=", mi.n_l3);
	con_hex("TTBR0", mi.ttbr);
	con_hex("SCTLR0", mi.sctlr_before);
	con_hex("SCTLR1", mi.sctlr_after);
	con_hex("TCR", rsr("tcr_el1"));
	con_skip();
	con_hex("PAR img", mmu_probe((uintptr_t)shim_main));
	con_hex("PAR fb", mmu_probe(FB_PHYS + 0x1000));
	con_hex("PAR gic", mmu_probe(GICD_BASE));
	con_skip();
	(void)fill_off;
	(void)mem_off;
	(void)fill_on;
	(void)mem_on;

	mark("psci");
	con_hex2("PSCI", "ver=", smc(PSCI_VERSION, 0, 0, 0),
		 "smccc=", smc(SMCCC_VERSION, 0, 0, 0));
	con_skip();
	test_gic();

	mark("done");
	reset(40);
#endif
}
