/*
 * sweet boot shim entry: arm64 Linux Image header so Qualcomm ABL treats
 * us like a kernel. ABL enters at EL1, MMU/D-cache off, x0 = DTB pointer.
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* code0 */
	.long	0			/* code1 */
	.quad	0			/* text_offset */
	.quad	_end			/* image_size (linked at 0) */
	.quad	0xa			/* flags: LE, 4K pages, place anywhere */
	.quad	0			/* res2 */
	.quad	0			/* res3 */
	.quad	0			/* res4 */
	.ascii	"ARM\x64"		/* magic */
	.long	0			/* res5 (PE offset) */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x19, x0			/* DTB pointer from ABL */
	msr	spsel, #1
	adr	x1, _stack_top
	mov	sp, x1
	adr	x1, vectors
#if defined(BOARD_CACTUS)
	mrs	x2, CurrentEL
	cmp	x2, #0x8
	b.eq	99f
	cmp	x2, #0x4
	b.ne	3f
	msr	vbar_el1, x1
	b	98f
99:	msr	vbar_el2, x1
98:
#else
	msr	vbar_el1, x1
#endif
	isb
#if defined(BOARD_CACTUS) && defined(SHIM_EARLY_PSTORE) && SHIM_EARLY_PSTORE
	/* Leave a breadcrumb before touching the BSS. */
	ldr	x1, =0x47c90000
	ldr	w2, =0x31424443		/* "CDB1" */
	str	w2, [x1]
	ldr	w2, =1
	str	w2, [x1, #4]
	dsb	sy
#endif
	/* Arm the top RGU before the BSS clear; no C globals are used yet. */
#if defined(SHIM_EARLY_WDT) && SHIM_EARLY_WDT
	bl	wdt_early_init
#endif
	/* zero .bss */
	adr	x1, _bss_start
	adr	x2, _bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x19
#if defined(BOARD_CACTUS) && defined(SHIM_STAGE15_EARLY) && SHIM_STAGE15_EARLY
	/* Minimum real-shim entry probe: stack, vectors and BSS only.
	 * No debug RAM, watchdog, framebuffer or XNU before USB. */
	bl	stage15_early_main
	b	3f
#endif
#if defined(BOARD_CACTUS) && defined(CACTUS_PMSG_TRACE) && CACTUS_PMSG_TRACE
	/* Earliest C-level breadcrumb: BSS and stack exist, and MMU is off.
	 * Write before watchdog, console, DT or payload initialization. */
	adr	x0, cactus_pmsg_start_msg
	mrs	x1, CurrentEL
	mov	x2, x19
	mrs	x3, mpidr_el1
	bl	pmsg_trace_mark
	mov	x0, x19
#endif
	bl	shim_main
3:	wfe
	b	3b

#if defined(BOARD_CACTUS)
	/* LK enters the cactus shim at EL2 (confirmed by Stage 3/4 hardware
	 * traces). XNU's primary entry expects EL1. Hand off only after setting
	 * up an AArch64 EL1 environment. x0 = entry PA, x1 = boot_args PA.
	 * The identical EL2->EL1 sequence is exercised by Stage 5 against TWRP. */
	.globl xnu_enter_el1
xnu_enter_el1:
	msr	daifset, #0xf
#if (defined(CACTUS_PMSG_TRACE) && CACTUS_PMSG_TRACE) || (defined(SHIM_STAGE15) && SHIM_STAGE15)
	/* Keep handoff arguments in callee-saved regs across the trace call. */
	mov	x19, x0
	mov	x20, x1
#endif
	mov	x16, x0
	mov	x0, x1
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mrs	x9, CurrentEL
	cmp	x9, #0x4
	b.eq	1f
	cmp	x9, #0x8
	b.ne	5f
	mov	x9, #(1 << 31)
	msr	HCR_EL2, x9
	msr	VTTBR_EL2, xzr
	msr	CPTR_EL2, xzr
	msr	MDCR_EL2, xzr
	mov	x9, #3
	msr	CNTHCTL_EL2, x9
	msr	CNTVOFF_EL2, xzr
	mrs	x9, ICC_SRE_EL2
	orr	x9, x9, #1
	orr	x9, x9, #8
	msr	ICC_SRE_EL2, x9
	movz	x9, #0x0800
	movk	x9, #0x30d0, lsl #16
	msr	SCTLR_EL1, x9
	adr	x9, _stack_top
	msr	SP_EL1, x9
	adr	x9, vectors
	msr	VBAR_EL1, x9
#if (defined(CACTUS_PMSG_TRACE) && CACTUS_PMSG_TRACE) || (defined(SHIM_STAGE15) && SHIM_STAGE15)
	/* ERET to our EL1 checkpoint, then branch to XNU. */
	adr	x9, 1f
#else
	mov	x9, x16
#endif
	msr	ELR_EL2, x9
	mov	x9, #0x3c5
	msr	SPSR_EL2, x9
	isb
	eret
1:
#if defined(SHIM_STAGE15) && SHIM_STAGE15
	/* EL1h shim-owned signal before entering XNU; USB MMIO is still PA. */
	mov	x0, x19
	mov	x1, x20
	bl	stage15_el1_ready
	mov	x0, x20
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x19
#elif defined(CACTUS_PMSG_TRACE) && CACTUS_PMSG_TRACE
	/* Proves the primary CPU reached EL1, before XNU executes. */
	adr	x0, cactus_pmsg_el1_msg
	mrs	x1, CurrentEL
	mov	x2, x19
	mov	x3, x20
	bl	pmsg_trace_mark
	mov	x0, x20
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	br	x19
#else
	br	x16
#endif
5:	wfe
	b	5b
#endif

#if defined(BOARD_CACTUS) && defined(CACTUS_PMSG_TRACE) && CACTUS_PMSG_TRACE
	.section .rodata, "a"
cactus_pmsg_start_msg:
	.asciz "shim start"
cactus_pmsg_el1_msg:
	.asciz "xnu el1 reached"
	.text
#endif

/* PSCI CPU_ON target. x0 = context id = cpu index (1..7). */
	.globl secondary_entry
secondary_entry:
	msr	daifset, #0xf
	msr	spsel, #1
	adr	x1, _sec_stacks
	add	x1, x1, x0, lsl #12
	add	x1, x1, #0x1000
	mov	sp, x1
	adr	x1, vectors
	msr	vbar_el1, x1
	isb
	bl	secondary_main
4:	wfe
	b	4b

/*
 * Exception vectors. Each slot stashes x29/x30, loads its slot number
 * and jumps to the common path, which saves x0-x28 and calls
 * exc_handler(slot, regs). IRQs return; everything else ends in the
 * on-screen panic.
 */
.macro vent slot
	.balign	0x80
	stp	x29, x30, [sp, #-16]!
	mov	x29, #\slot
	b	exc_common
.endm

	.balign	0x800
	.globl vectors
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	sub	sp, sp, #(32 * 8)
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	str	x28, [sp, #224]
	/* The vent prologue stashed the faulting x29/x30 above this frame. */
	ldr	x2, [sp, #256]
	ldr	x3, [sp, #264]
	str	x2, [sp, #232]
	str	x3, [sp, #240]
	mov	x0, x29
	mov	x1, sp
	bl	exc_handler
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldr	x28, [sp, #224]
	add	sp, sp, #(32 * 8)
	ldp	x29, x30, [sp], #16
	eret
