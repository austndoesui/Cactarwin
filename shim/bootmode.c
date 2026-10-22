#include "bootmode.h"
#include "board.h"
#include "debug.h"

#if defined(BOARD_CACTUS)

#define PWRAP_WACS2_CMD		(PWRAP_BASE + 0xC20UL)
#define PWRAP_WACS2_RDATA	(PWRAP_BASE + 0xC24UL)
#define PWRAP_WACS2_VLDCLR	(PWRAP_BASE + 0xC28UL)

#define PWRAP_INIT_DONE		(1U << 22)
#define PWRAP_FSM_MASK		(7U << 16)
#define PWRAP_FSM_IDLE		(0U << 16)
#define PWRAP_FSM_WFVLDCLR	(6U << 16)
#define PWRAP_TIMEOUT		1000000U

#define RTC_BBPU		0x588U
#define RTC_PDN1		0x5B4U
#define RTC_PROT		0x5BCU
#define RTC_WRTGR		0x5C2U

#define RTC_PROT_UNLOCK1	0x586AU
#define RTC_PROT_UNLOCK2	0x9136U
#define RTC_PDN1_FAC_RESET	(1U << 4)
#define RTC_PDN1_FAST_BOOT	(1U << 13)
#define RTC_BBPU_CBUSY		(1U << 6)

#define RTC_TRIGGER_US		2000U
#define RTC_BUSY_TIMEOUT	100000U

static inline uint32_t rd32(uintptr_t a)
{
	return *(volatile uint32_t *)a;
}

static inline void wr32(uintptr_t a, uint32_t v)
{
	*(volatile uint32_t *)a = v;
}

static inline uint64_t rd_cntpct(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static inline uint64_t rd_cntfrq(void)
{
	uint64_t v;
	__asm__ volatile("isb; mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static void delay_us(uint32_t us)
{
	uint64_t f = rd_cntfrq();
	uint64_t start;

	if (!us)
		return;
	if (!f) {
		for (volatile uint32_t i = 0; i < us * 100U; i++)
			;
		return;
	}
	start = rd_cntpct();
	while (rd_cntpct() - start < (f / 1000000U) * us)
		;
}

static int pwrap_wait_idle(void)
{
	for (uint32_t i = 0; i < PWRAP_TIMEOUT; i++) {
		uint32_t v = rd32(PWRAP_WACS2_RDATA);

		if (!(v & PWRAP_INIT_DONE))
			return -3;
		if ((v & PWRAP_FSM_MASK) == PWRAP_FSM_IDLE)
			return 0;
	}
	return -1;
}

static int pwrap_write(uint16_t addr, uint16_t data)
{
	int rc = pwrap_wait_idle();

	if (rc)
		return rc;
	wr32(PWRAP_WACS2_CMD,
	     (1U << 31) | (((uint32_t)addr >> 1) << 16) | data);
	__asm__ volatile("dsb sy" ::: "memory");
	return 0;
}

static int pwrap_read(uint16_t addr, uint16_t *data)
{
	uint32_t v;
	int rc = pwrap_wait_idle();

	if (rc)
		return rc;
	wr32(PWRAP_WACS2_CMD, ((uint32_t)addr >> 1) << 16);
	__asm__ volatile("dsb sy" ::: "memory");

	for (uint32_t i = 0; i < PWRAP_TIMEOUT; i++) {
		v = rd32(PWRAP_WACS2_RDATA);
		if (!(v & PWRAP_INIT_DONE))
			return -3;
		if ((v & PWRAP_FSM_MASK) == PWRAP_FSM_WFVLDCLR) {
			*data = v & 0xffff;
			wr32(PWRAP_WACS2_VLDCLR, 1);
			__asm__ volatile("dsb sy" ::: "memory");
			return 0;
		}
	}
	return -2;
}

static int rtc_trigger(void)
{
	uint16_t bbpu;
	int rc;

	dbg_mark("rtc trigger", 0, 0, 0);
	rc = pwrap_write(RTC_WRTGR, 1);
	if (rc)
		return rc;
	delay_us(100);

	for (uint32_t i = 0; i < RTC_BUSY_TIMEOUT; i++) {
		rc = pwrap_read(RTC_BBPU, &bbpu);
		if (rc)
			return rc;
		if (!(bbpu & RTC_BBPU_CBUSY)) {
			delay_us(RTC_TRIGGER_US);
			return 0;
		}
	}
	return -5;
}

static int rtc_unlock(void)
{
	int rc;

	dbg_mark("rtc unlock", 0, 0, 0);
	rc = pwrap_write(RTC_PROT, RTC_PROT_UNLOCK1);
	if (rc)
		return rc;
	rc = rtc_trigger();
	if (rc)
		return rc;
	rc = pwrap_write(RTC_PROT, RTC_PROT_UNLOCK2);
	if (rc)
		return rc;
	rc = rtc_trigger();
	if (!rc)
		dbg_mark("rtc unlocked", 0, 0, 0);
	return rc;
}

static int rtc_read_pdn1(uint16_t *pdn1)
{
	int rc = pwrap_read(RTC_PDN1, pdn1);

	return rc;
}

static int __attribute__((unused)) rtc_set_pdn1(uint16_t set, uint16_t clear,
			uint16_t *oldp, uint16_t *newp)
{
	uint16_t old, new, check;
	int rc;

	rc = rtc_read_pdn1(&old);
	if (rc)
		return rc;
	new = (uint16_t)((old & ~clear) | set);
	if (oldp)
		*oldp = old;
	if (newp)
		*newp = new;
	dbg_mark("rtc set", old, new, 0);
	if (new == old)
		return 0;

	rc = rtc_unlock();
	if (rc)
		return rc;
	dbg_mark("rtc write", old, new, 0);
	rc = pwrap_write(RTC_PDN1, new);
	if (rc)
		return rc;
	rc = rtc_trigger();
	if (rc)
		return rc;
	rc = rtc_read_pdn1(&check);
	if (rc)
		return rc;
	dbg_mark("rtc check", check, new, 0);
	if (check != new)
		return -4;
	return 0;
}

int bootmode_probe(uint16_t *pdn1)
{
	uint16_t v = 0;
	int rc;

	if (pdn1)
		*pdn1 = 0;
	rc = rtc_read_pdn1(&v);
	if (pdn1)
		*pdn1 = v;
	dbg_mark("rtc probe", (uint32_t)rc, v, 0);
	return rc;
}

void bootmode_prepare_reset(void)
{
#if AUTO_BOOT_TARGET != BOOTMODE_OFF
	static int prepared;
	uint16_t old = 0, new = 0;
	uint16_t set, clear;
	int rc;

	if (prepared)
		return;

#if AUTO_BOOT_TARGET == BOOTMODE_FASTBOOT
	set = RTC_PDN1_FAST_BOOT;
	clear = 0;
#elif AUTO_BOOT_TARGET == BOOTMODE_RECOVERY
	set = RTC_PDN1_FAC_RESET;
	clear = 0;
#else
	return;
#endif

	dbg_mark("rtc prepare", set, clear, AUTO_BOOT_TARGET);
	rc = rtc_set_pdn1(set, clear, &old, &new);
	if (rc) {
		dbg_mark("rtc fail", (uint32_t)rc, old, AUTO_BOOT_TARGET);
		return;
	}
	prepared = 1;
	dbg_mark(AUTO_BOOT_TARGET == BOOTMODE_FASTBOOT ?
		 "rtc fastboot" : "rtc recovery", old, new, 0);
#else
	return;
#endif
}

#else

int bootmode_probe(uint16_t *pdn1)
{
	if (pdn1)
		*pdn1 = 0;
	return -1;
}

void bootmode_prepare_reset(void)
{
}

#endif
