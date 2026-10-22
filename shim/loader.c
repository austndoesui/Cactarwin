/*
 * Act as iBoot for xnu.
 *
 * mkpayload.py already resolved the kernel's chained fixups and built the
 * ADT, so this only has to: find the payload in the bootloader ramdisk, copy
 * the segments to phys_base + (vmaddr - virt_base), place the ADT and
 * boot_args behind the kernel, and jump to the entry point with the MMU
 * and caches off, x0 = boot_args (physical).
 *
 * This path intentionally validates every offset before dereferencing it.
 * With no UART on cactus, a blue shim fault screen is the only early-boot
 * evidence we get, so a bad payload must fail with a readable message.
 */
#include <stdint.h>
#include "console.h"
#include "debug.h"
#include "fdt.h"
#include "loader.h"
#include "watchdog.h"
#if defined(BOARD_CACTUS)
#include "fdt_ranges.h"
#include "payload_layout.h"
extern char _head[], _end[];
#endif
#if defined(SHIM_STAGE15) && SHIM_STAGE15
#include "stage15.h"
#endif

#define BOOT_LINE_LENGTH	608
#define PAGE_16K		0x4000UL
#define PAYLOAD_HDR_SIZE	0x2a8UL
/* Apple DeviceTree begins with LE property/child counts, not FDT magic. */

struct payload_seg {
	uint64_t vmaddr, vmsize, file_off, file_size;
};

struct payload {
	char magic[8];
	uint32_t version, nsegs;
	uint64_t virt_base, entry_va, adt_off, adt_size, kernel_end;
	uint64_t phys_base, mem_size;
	char cmdline[BOOT_LINE_LENGTH];
	struct payload_seg segs[];
};

/* pexpert/pexpert/arm64/boot.h, Revision/Version 2 */
struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct boot_args {
	uint16_t Revision, Version;
	uint64_t virtBase, physBase, memSize, topOfKernelData;
	struct boot_video Video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char CommandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags, memSizeActual;
};

_Static_assert(__builtin_offsetof(struct payload, segs) == PAYLOAD_HDR_SIZE, "payload header layout");
_Static_assert(__builtin_offsetof(struct boot_args, Video) == 40, "boot_args layout");
_Static_assert(__builtin_offsetof(struct boot_args, CommandLine) == 108, "boot_args layout");
_Static_assert(sizeof(struct boot_args) == 736, "boot_args layout");

static inline uint32_t rd32(uintptr_t a)
{
	return *(volatile uint32_t *)a;
}

static inline uint8_t rd8(uintptr_t a)
{
	return *(volatile uint8_t *)a;
}

/* MMU off: everything is Device memory. Bulk copies are 8-byte aligned. */
static void copy_bytes(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	uint64_t i = 0;

	for (; i + 8 <= n; i += 8)
		d[i / 8] = s[i / 8];
	for (; i < n; i++)
		((volatile uint8_t *)dst)[i] = ((volatile const uint8_t *)src)[i];
}

static void zero_bytes(uintptr_t dst, uint64_t n)
{
	while (n && (dst & 7)) {
		*(volatile uint8_t *)dst = 0;
		dst++;
		n--;
	}

	volatile uint64_t *d = (volatile uint64_t *)dst;
	uint64_t i = 0;

	for (; i + 8 <= n; i += 8)
		d[i / 8] = 0;
	for (; i < n; i++)
		((volatile uint8_t *)dst)[i] = 0;
}

static int magic_ok(const char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

static int range_ok(uint64_t v, uint64_t len, uint64_t base, uint64_t size)
{
	return v >= base && v - base <= size && len <= size - (v - base);
}

#if defined(BOARD_CACTUS)
/* The cactus bootloader enters at EL2. XNU requires the boot CPU at EL1. */
extern void xnu_enter_el1(uintptr_t entry_pa, uintptr_t boot_args_pa)
    __attribute__((noreturn));
#else
typedef void (*xnu_entry_t)(uint64_t boot_args_pa);
#endif

const char *loader_boot(uintptr_t dtb)
{
	uint32_t fdt_size;
	uint64_t rd_start, rd_end;
#if defined(BOARD_CACTUS)
	struct fdt_info live;
#endif

	dbg_stage("loader enter");
	wdt_ping();

	if (!fdt_check(dtb, &fdt_size)) {
		con_hex("DTB", dtb);
		con_str("DTB", "BAD HEADER");
		return "bad DTB";
	}
#if defined(BOARD_CACTUS)
	if (fdt_inspect((const void *)dtb, fdt_size, &live)) {
#if defined(SHIM_STAGE15) && SHIM_STAGE15
		stage15_mark(S15_ERROR, 1, dtb, fdt_size);
#endif
		return "invalid FDT memory/reservation map";
	}
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_mark(S15_DTB_OK, live.memory_count,
	             live.reserved_count, fdt_size);
#endif
#endif
	con_hex2("DTB", "size=", fdt_size,
		 "magic=", __builtin_bswap32(rd32(dtb)));
	dbg_stage("loader dtb");

	if (!fdt_chosen_u64(dtb, "linux,initrd-start", &rd_start) ||
	    !fdt_chosen_u64(dtb, "linux,initrd-end", &rd_end)) {
		con_str("CHOSEN", "NO INITRD");
		fdt_dump(dtb);
		return "no initrd in /chosen";
	}

	con_hex("INITRD", rd_start);
	con_hex("IRDEND", rd_end);

	if (rd_end <= rd_start) {
		con_str("INITRD", "BAD RANGE");
		return "bad initrd range";
	}
	if ((rd_start & 7) || (rd_end & 7)) {
		con_str("INITRD", "UNALIGNED");
		return "unaligned initrd";
	}
	dbg_stage("loader initrd");

	uint64_t payload_size = rd_end - rd_start;
	con_hex("IRDSZ", payload_size);
	if (payload_size < PAYLOAD_HDR_SIZE) {
		con_str("PAYLOAD", "TOO SMALL");
		return "payload too small";
	}

	const struct payload *p = (const struct payload *)rd_start;
	if (!magic_ok(p->magic)) {
		con_hex("PAYLOAD", rd_start);
		con_str("PAYLOAD", "BAD MAGIC");
		return "no SWEETXNU payload";
	}
	if (p->version != 1) {
		con_hex2("PAYLOAD", "version=", p->version, 0, 0);
		return "bad payload version";
	}
	if (p->nsegs == 0 || p->nsegs > 64) {
		con_hex2("PAYLOAD", "nsegs=", p->nsegs, 0, 0);
		return "bad segment count";
	}
	dbg_mark("loader payload", p->nsegs, p->phys_base, p->mem_size);

	uint64_t hdr_size = PAYLOAD_HDR_SIZE + (uint64_t)p->nsegs * 32;
	if (hdr_size > payload_size) {
		con_hex("PAYLOAD", hdr_size);
		return "segment table too large";
	}
#if defined(BOARD_CACTUS)
	/* Mandatory for EVERY cactus XNU build, including normal STAGE15=0:
	 * reject unsafe physical windows and segments before any copy. */
	struct xnu_layout verified;
	enum xnu_validation verdict = xnu_validate_payload(
		p, payload_size, &live, dtb, fdt_size,
		(uintptr_t)_head, (uintptr_t)_end - (uintptr_t)_head, &verified);
	if (verdict != XNU_VALIDATE_OK) {
		dbg_mark("unsafe xnu RAM", verdict, p->phys_base, p->mem_size);
		return "XNU payload memory/layout validation rejected";
	}
#endif
	if (p->adt_off & 7) {
		con_str("PAYLOAD", "BAD ADT OFF");
		return "bad ADT alignment";
	}
	if (p->adt_size < 40 || p->adt_off + p->adt_size < p->adt_off ||
	    p->adt_off + p->adt_size > payload_size) {
		con_hex("ADT", p->adt_off);
		con_hex("ADTSZ", p->adt_size);
		return "bad ADT range";
	}
	if (p->mem_size == 0 || (p->phys_base & (PAGE_16K - 1))) {
		con_hex("PHYS", p->phys_base);
		con_hex("MEMSZ", p->mem_size);
		return "bad physical range";
	}
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	/* XNU boot_args gives the kernel this whole RAM interval. Do not
	 * hand XNU a span with DT reservations, DTB, shim or payload in it. */
	if (!fdt_region_free(&live, p->phys_base, p->mem_size) ||
	    fdt_region_overlap(p->phys_base, p->mem_size, dtb, fdt_size) ||
	    fdt_region_overlap(p->phys_base, p->mem_size,
	                       live.initrd_start, live.initrd_end - live.initrd_start) ||
	    fdt_region_overlap(p->phys_base, p->mem_size,
	                       (uintptr_t)_head, (uintptr_t)_end - (uintptr_t)_head)) {
		stage15_mark(S15_ERROR, 2, p->phys_base, p->mem_size);
		return "XNU RAM window intersects reserved/live boot memory";
	}
#endif

	uint64_t delta = p->phys_base - p->virt_base;
	con_hex("PHYS", p->phys_base);
	con_hex("MEMSZ", p->mem_size);
	con_hex("VIRT", p->virt_base);
	con_hex("ENTRY", p->entry_va);
	con_hex("KEND", p->kernel_end);
	con_hex2("SEGS", "n=", p->nsegs, "ver=", p->version);
	con_hex2("ADT", "off=", (uint32_t)p->adt_off, "sz=", (uint32_t)p->adt_size);
	con_str("CMD", p->cmdline);

	/* mkadt.py emits Apple's ADT node layout: two LE counts followed by
	 * 32-byte property name and LE property length.  The root begins with
	 * a "name" property.  Reject an FDT or a clearly corrupt ADT before
	 * copying anything into the target kernel region. */
	uintptr_t adt = rd_start + p->adt_off;
	uint32_t props = rd32(adt), children = rd32(adt + 4);
	con_hex2("ADTROOT", "props=", props, "children=", children);
	if (p->adt_size < 44 || props == 0 || props > 256 || children > 256 ||
	    rd32(adt + 8) != 0x656d616eU || rd8(adt + 12) != 0 ||
	    rd32(adt + 40) == 0 || rd32(adt + 40) > p->adt_size - 44) {
		con_str("ADT", "BAD APPLE ADT");
		return "invalid Apple ADT root";
	}
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_mark(S15_PAYLOAD_OK, p->nsegs, p->entry_va, p->phys_base);
#endif

	/* The payload must not overlap where the kernel goes. */
	if (rd_end > p->phys_base && rd_start < p->phys_base + p->mem_size) {
		con_hex("PAYEND", rd_end);
		con_str("PAYLOAD", "OVERLAPS RAM");
		return "payload/ramdisk overlaps kernel memory";
	}

	uint64_t max_end = p->virt_base;
	int entry_ok = 0;

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uint64_t dst = s->vmaddr + delta;
		uint64_t vend = s->vmaddr + s->vmsize;

		if (s->file_off + s->file_size < s->file_off ||
		    s->file_off + s->file_size > payload_size) {
			con_hex2("SEG", "n=", i, "bad off=", (uint32_t)s->file_off);
			return "segment file range";
		}
		if (s->vmsize < s->file_size) {
			con_hex2("SEG", "n=", i, "vmsize=", (uint32_t)s->vmsize);
			return "segment size";
		}
		if (!range_ok(dst, s->vmsize, p->phys_base, p->mem_size) ||
		    !range_ok(s->vmaddr, s->vmsize, p->virt_base, p->mem_size)) {
			con_hex2("SEG", "n=", i, "bad dst=", (uint32_t)dst);
			return "segment outside RAM";
		}
		if (s->file_off & 7) {
			con_hex2("SEG", "n=", i, "off=", (uint32_t)s->file_off);
			return "segment file unaligned";
		}
		if (vend > max_end)
			max_end = vend;
		if (p->entry_va >= s->vmaddr &&
		    p->entry_va - s->vmaddr < s->file_size)
			entry_ok = 1;
#if defined(SHIM_STAGE15) && SHIM_STAGE15
		/* All mappings in one XNU-managed window must be disjoint, and
		 * none can overlap live firmware/probe code or reserved RAM. */
		for (uint32_t j = 0; j < i; ++j)
			if (fdt_region_overlap(s->vmaddr, s->vmsize,
			                       p->segs[j].vmaddr, p->segs[j].vmsize)) {
				stage15_mark(S15_ERROR, 3, i, j);
				return "overlapping Mach-O segments";
			}
#endif

		con_hex2("SEG", "n=", i, "fsz=", (uint32_t)s->file_size);
		con_hex(" VA", s->vmaddr);
		con_hex(" DST", dst);
		con_hex(" SZ", s->vmsize);
	}

	con_hex("MAXEND", max_end);
	if (!entry_ok) {
		con_hex("ENTRY", p->entry_va);
		return "entry not in any segment";
	}
	if (p->kernel_end < max_end) {
		con_hex("KEND", p->kernel_end);
		con_hex("MAXEND", max_end);
		return "kernel_end below segments";
	}

	uint64_t adt_pa = p->kernel_end + delta;
	uint64_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uint64_t top = ba_pa + PAGE_16K;

	con_hex("ADTPA", adt_pa);
	con_hex("ADTVA", adt_pa - delta);
	con_hex("BAPA", ba_pa);
	con_hex("TOP", top);

	if (!range_ok(adt_pa, p->adt_size, p->phys_base, p->mem_size) ||
	    !range_ok(ba_pa, PAGE_16K, p->phys_base, p->mem_size) ||
	    top < ba_pa) {
		return "ADT/boot_args outside RAM";
	}
	if (adt_pa < p->phys_base + (max_end - p->virt_base)) {
		con_hex("ADTPA", adt_pa);
		return "ADT overlaps kernel";
	}
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_mark(S15_LAYOUT_OK, p->phys_base, ba_pa, top);
#if defined(SHIM_STAGE15_DIAG) && SHIM_STAGE15_DIAG
	/* No copy or XNU branch in the Stage 15 hardware preflight build. */
	return "stage15 validated; no kernel copy";
#endif
#endif

	dbg_stage("loader copy");
	/* Copy segments after all validation so a malformed payload changes nothing. */
	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + delta;
		dbg_mark("seg copy", i, dst, s->file_size);
		copy_bytes(dst, rd_start + s->file_off, s->file_size);
		if (s->vmsize > s->file_size)
			zero_bytes(dst + s->file_size, s->vmsize - s->file_size);
#if defined(SHIM_STAGE15) && SHIM_STAGE15
		stage15_mark(S15_SEGMENT, i, dst, s->vmsize);
#endif
		wdt_ping();
	}
	dbg_stage("loader adt");
	copy_bytes(adt_pa, rd_start + p->adt_off, p->adt_size);

	zero_bytes(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = FB_BASE;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - delta;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->CommandLine[BOOT_LINE_LENGTH - 1] = 0;
	ba->bootFlags = 0;
	ba->memSizeActual = p->mem_size;
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	stage15_mark(S15_BOOTARGS, ba_pa, ba->deviceTreeP, top);
#endif

	con_hex2("BOOTARG", "revision=", ba->Revision, "version=", ba->Version);
	con_hex("DEVTREE", ba->deviceTreeP);
	con_hex2("DTLEN", "len=", ba->deviceTreeLength, 0, 0);
	con_hex("TOPKERN", ba->topOfKernelData);
	con_hex2("VIDEO", "base=", (uint32_t)ba->Video.v_baseAddr,
		 "stride=", (uint32_t)ba->Video.v_rowBytes);

	uintptr_t entry_pa = p->entry_va + delta;
	con_hex("JUMP", entry_pa);
	con_hex("JUMPTOP", top);
	con_str("XNU", "jumping (fbcon clears screen)");

	/* Everything was written with the MMU and caches off. */
	dbg_mark("xnu jump", entry_pa, ba_pa, top);
	dbg_flush();
#if defined(BOARD_CACTUS) && defined(SHIM_TRACE_WDT) && SHIM_TRACE_WDT
	/* Diagnostic XNU boots retain the armed watchdog for a warm reset. */
	dbg_stage("xnu wdt armed");
#else
	wdt_disable();
#endif
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
#if defined(BOARD_CACTUS)
	/* Last shim-side milestone; next marker is emitted at EL1 after ERET. */
	dbg_mark("xnu handoff", entry_pa, ba_pa, top);
	xnu_enter_el1(entry_pa, ba_pa);
#else
	((xnu_entry_t)entry_pa)(ba_pa);

	/* If XNU returns, re-arm recovery and report it. */
	wdt_init(20);
	dbg_stage("xnu return");
	return "xnu returned";
#endif
}
