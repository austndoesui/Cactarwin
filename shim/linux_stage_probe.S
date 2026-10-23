/* Isolated, observable cactus boot-stage probes.
 * STAGE=1: test reserved ramoops RAM read/write/cache clean, then TWRP.
 * STAGE=2: test read-only MTK watchdog register access, then TWRP.
 * STAGE=3: write a live record to a second (raw CPIO) initramfs archive.
 * STAGE=4: call the real dbg_init(), copy the debug RAM trace into CPIO.
 * STAGE=5: transition EL2 to EL1, record the resulting EL, then TWRP.
 * STAGE=6: write a native ramoops pmsg line, then TWRP (transport test).
 * STAGE=7: read-only MT6765 MUSB/USB2 PHY snapshot, then TWRP.
 * STAGE=8: isolated 10s MUSB CDC-ACM enumeration attempt, then TWRP.
 * STAGE=9: C ABI and MUSB identity-only test, no USB writes, then TWRP.
 * STAGE=10: bounded 1s MUSB soft-connect test, then TWRP.
 * STAGE=11: bounded 2s MUSB IRQ/EP0 status sampling, then TWRP.
 * STAGE=12: guarded CDC ACM with raw binary marker, then TWRP.
 * STAGE=13: USB2 PHY device-mode recovery and IRQ sampling, then TWRP.
 * STAGE=14: reusable CDC with varying binary trace frames, then TWRP.
 * STAGE=15: Stage 15 early USB C logger using the verified Stage 14 wrapper.
 * STAGE=16: Real shim FDT/debug preflight inside proven recovery wrapper.
 * STAGE=17: Report live RAM immediately after FDT parse, before delay.
 * STAGE=18: Validate relocated XNU payload through real loader admission.
 * STAGE=19: Load and sample-check XNU segments, ADT and boot_args, then TWRP.
 * STAGE=20: Same loader; shim-owned EL1 ERET/HVC/EL2 proof, then TWRP.
 * STAGE=21: Stage20-B; execute the real XNU branch into a temporary
 *           BRK at its target, capture EL1 exception, then TWRP.
 * STAGE=22: Stage20-C; run XNU's framebuffer marker until the VBAR
 *           handoff, BRK before installing XNU vectors, then TWRP.
 * STAGE=23: Stage20-D; execute VBAR write, trap via instrumented
 *           XNU EL1h synchronous vector, and recover to TWRP.
 * STAGE=24: Stage21-C1; execute XNU 4K page table assembly until the
 *           pre-MMU virtual VBAR write, EL1t BRK -> vector HVC -> EL2
 *           table/sysreg diagnostics, then recover to TWRP.
 * STAGE=25: Stage21-C2; execute the MMU-enable MSR and ISB, trap at
 *           the first translated instruction through the virtual EL1t
 *           vector and log EL2 physical state before returning to TWRP.
 * STAGE=26: Stage21-D; reach first high-VA C prologue through an EL1t
 *           virtual-vector BRK/HVC, then return to TWRP.
 * STAGE=27: Combined Stage22; snapshot CPU/timer/boot memory at EL2,
 *           run deeper high-VA XNU C startup, trap later or log fault,
 *           then return to TWRP through the proven EL2 path.
 * STAGE=28: Stage23; test visible splash RAM while COM25 is active,
 *           run XNU past Stage22 checkpoint, capture a real sync fault
 *           and hold USB at EL2 instead of intentionally rebooting.
 * No watchdog writes, RTC, framebuffer, or XNU. Stage 4 initializes the
 * existing reserved-RAM trace, but does not configure the kernel's ramoops.
 * A failure can hang; hold power/return to fastboot and restore boot.
 */
#ifndef STAGE
#error Specify STAGE=1 through 30
#endif
#if STAGE < 1 || STAGE > 30
#error Unsupported STAGE
#endif
#if STAGE >= 3 && !defined(TRACE_ADDR)
#error TRACE_ADDR must be the runtime address of the raw CPIO record
#endif
    .section .text, "ax"
    .balign 8
    .global _start
_start:
    b probe
    .long 0
    .quad 0
    .quad image_end - _start
    .quad 0xa
    .quad 0
    .quad 0
    .quad 0
    .ascii "ARM\x64"
    .long 0
kernel_begin:
    .incbin "out/cactus/linux-chainload/recovery-Image"
kernel_end:
    .balign 8
probe:
    msr daifset, #0xf
    mov x19, x0
    msr spsel, #1
    adr x1, probe_stack_top
    mov sp, x1
    adr x1, probe_vectors
#if STAGE >= 16
    /* LK enters at EL2 on cactus; install the fault vectors at that EL. */
    mrs x2, CurrentEL
    cmp x2, #8
    b.ne 99f
    msr vbar_el2, x1
    b 98f
99:
    msr vbar_el1, x1
98:
#else
    msr vbar_el1, x1
#endif
    isb
    adr x1, probe_bss
    adr x2, probe_bss_end
1:  cmp x1, x2
    b.hs stage_check
    str xzr, [x1], #8
    b 1b
stage_check:
#if STAGE == 5
    /* Stage 3/4 established that LK enters at EL2 on this device.  Exercise
     * the same single-core transition used by the XNU handoff, but run the
     * known-good Linux recovery kernel afterward.  EL1 uses its own stack. */
    mrs x10, CurrentEL
    cmp x10, #0x8
    b.ne stage_5_after_el_drop
    mov x10, #(1 << 31)
    msr HCR_EL2, x10             /* EL1 executes AArch64, stage 2 disabled */
    msr VTTBR_EL2, xzr
    msr CPTR_EL2, xzr
    msr MDCR_EL2, xzr
    mov x10, #3
    msr CNTHCTL_EL2, x10         /* Allow EL1 virtual/physical counters */
    msr CNTVOFF_EL2, xzr
    mrs x10, ICC_SRE_EL2
    orr x10, x10, #1
    orr x10, x10, #8
    msr ICC_SRE_EL2, x10
    movz x10, #0x0800
    movk x10, #0x30d0, lsl #16
    msr SCTLR_EL1, x10
    adr x10, probe_stack_top
    msr SP_EL1, x10
    adr x10, stage_5_after_el_drop
    msr ELR_EL2, x10
    mov x10, #0x3c5             /* Mask interrupts, return to EL1h */
    msr SPSR_EL2, x10
    isb
    eret
stage_5_after_el_drop:
    adr x10, probe_vectors
    msr VBAR_EL1, x10
    isb
#endif
#if STAGE == 1
    /* Write only one aligned word of the known reserved ramoops region.
     * Do not depend on retaining/reading it after reboot or recovery.
     * Save and restore original contents before chainloading Linux.
     */
    movz x9, #0x0000
    movk x9, #0x47c9, lsl #16
    ldr w10, [x9]
    movz w11, #0x4d41
    movk w11, #0x5453, lsl #16
    str w11, [x9]
    dsb sy
    dc cvac, x9
    dsb sy
    ldr w12, [x9]
    str w10, [x9]
    dsb sy
    dc cvac, x9
    dsb sy
    cmp w12, w11
    b.ne probe_fault
#elif STAGE == 2
    /* Read-only WDT mode and status: verify MMIO access before testing
     * watchdog programming in a separate, explicit experiment.
     */
    movz x9, #0x7000
    movk x9, #0x1000, lsl #16
    ldr w10, [x9]
    ldr w11, [x9, #0xc]
    dsb sy
#elif STAGE >= 3
    /* The extra CPIO file is at a known offset in the LK-loaded ramdisk.
     * Check the sentinel before writing: if LK used a different address,
     * leave the memory untouched and still hand off to TWRP. */
    ldr x9, =TRACE_ADDR
    ldr w10, [x9]
    movz w11, #0x5443                /* ASCII "CTRC", little-endian */
    movk w11, #0x4352, lsl #16
    cmp w10, w11
    b.ne chainload
    ldr w10, [x9, #4]
    cmp w10, #1
    b.ne chainload

#if STAGE == 4
    /* This is the actual C implementation used by the XNU shim. */
    bl dbg_init
    ldr x9, =TRACE_ADDR           /* C may clobber caller-saved x9. */
    /* Capture the header and ring before the recovery ramoops driver runs.
     * The Linux initramfs extractor copies this raw CPIO content to rootfs. */
    movz x10, #0x0000
    movk x10, #0x47c9, lsl #16
    add x11, x9, #64
    mov x12, #0x5000
3:  ldr x13, [x10], #8
    str x13, [x11], #8
    subs x12, x12, #8
    b.ne 3b
#endif

    mov w10, #STAGE
    str w10, [x9, #8]
    mov w10, #1
    str w10, [x9, #12]
    str x19, [x9, #16]               /* LK's device tree pointer */
    mrs x10, CurrentEL
    str x10, [x9, #24]
    mrs x10, mpidr_el1
    str x10, [x9, #32]
    mrs x10, cntfrq_el0
    str x10, [x9, #40]
    movz x10, #0x7000
    movk x10, #0x1000, lsl #16
    ldr w11, [x10]
    str w11, [x9, #48]
    ldr w11, [x10, #0xc]
    str w11, [x9, #52]
#if STAGE == 7
    /* USB7: sixteen LE32 words at capsule +64. MTK USB core and PHY
     * windows come from the live cactus DT. Only read MMIO; don't
     * modify USB clocks, PHY power, endpoint state or soft-connect. */
    add x11, x9, #64
    movz w12, #0x5355
    movk w12, #0x3742, lsl #16  /* USB7 */
    str w12, [x11, #0]
    movz x10, #0x0000
    movk x10, #0x1120, lsl #16
    str w10, [x11, #4]         /* base address */
    ldrh w12, [x10, #0x6c]    /* HWVERS */
    str w12, [x11, #8]
    ldrb w12, [x10, #1]       /* POWER */
    str w12, [x11, #12]
    ldrb w12, [x10, #0x60]    /* DEVCTL */
    str w12, [x11, #16]
    ldrb w12, [x10, #0x78]    /* EPINFO */
    str w12, [x11, #20]
    ldrb w12, [x10, #0x79]    /* RAMINFO */
    str w12, [x11, #24]
    ldr w12, [x10, #0xa4]     /* USB_L1INTM */
    str w12, [x11, #28]
    movz x10, #0x0800
    movk x10, #0x11cc, lsl #16 /* PHY base +0x800 */
    ldr w12, [x10, #0x18]    /* PHY REV/VBUS */
    str w12, [x11, #32]
    ldr w12, [x10, #0x68]    /* UART/suspend/DP-DM */
    str w12, [x11, #36]
    ldr w12, [x10, #0x6c]    /* device mode */
    str w12, [x11, #40]
    ldr w12, [x10, #0x20]    /* GPIO */
    str w12, [x11, #44]
    ldr w12, [x10, #4]       /* PHY trim */
    str w12, [x11, #48]
    movz x10, #0x0000
    movk x10, #0x1120, lsl #16
    ldrb w12, [x10, #0xb]   /* INTRUSBE (not read-to-clear status) */
    str w12, [x11, #52]
    movz w12, #0x0800
    movk w12, #0x11cc, lsl #16
    str w12, [x11, #56]      /* PHY register base */
    movz w12, #0x4e45
    movk w12, #0x3744, lsl #16  /* END7 */
    str w12, [x11, #60]
#endif
#if STAGE >= 8 && STAGE <= 13
    /* C gadget proof is strictly time-limited and resumes recovery.
     * It uses 64 bytes following the generic trace header, not pstore. */
    add x0, x9, #64
    bl musb_cdc_probe
    ldr x9, =TRACE_ADDR
#endif
#if STAGE == 14
    add x0, x9, #64
    bl stage14_usb_probe
    ldr x9, =TRACE_ADDR
#endif
#if STAGE == 15
    /* Run Stage15 early logger from the known-good Stage14 entry with
     * its CPIO sentinel and preserved Linux/TWRP recovery wrapper. */
    mov x0, x19
    bl stage15_early_main
    ldr x9, =TRACE_ADDR
#endif
#if STAGE == 16
    /* Stage16 reuses the proven wrapper; the real shim components
     * are invoked by the polled CDC probe with observable brackets. */
    mov x0, x19
    bl stage16_probe
    ldr x9, =TRACE_ADDR
#endif
#if STAGE == 17
    /* Keep Stage14/15/16's working Linux + TWRP wrapper intact. */
    mov x0, x19
    bl stage17_probe
    ldr x9, =TRACE_ADDR
#endif
#if STAGE == 18
    /* Validate the copied payload in the established USB/TWRP wrapper.
     * No XNU segments are copied and no XNU entry is taken. */
    mov x0, x19
    bl stage18_probe
    ldr x9, =TRACE_ADDR
#endif
#if STAGE >= 19
    /* Stage19-A performs real writes only after the Stage18 live
     * admission gate; USB and TWRP chainload remain in control. */
    mov x0, x19
    bl stage19_probe
    ldr x9, =TRACE_ADDR
#endif
    /* Commit marker only after all checkpoint fields were written. */
    movz w10, #0x5443                /* ASCII "CTOK" */
    movk w10, #0x4b4f, lsl #16
    str w10, [x9]
    dsb sy
    bic x10, x9, #63
#if STAGE == 4
    add x11, x9, #0x5000
    add x11, x11, #64
#elif STAGE >= 7
    add x11, x9, #128
#else
    add x11, x9, #64
#endif
4:  dc cvac, x10
    add x10, x10, #64
    cmp x10, x11
    b.lo 4b
    dsb sy
#endif
#if STAGE == 6
    adr x0, stage_6_message
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    bl pmsg_trace_mark
#endif
    b chainload
probe_fault:
    wfe
    b probe_fault

chainload:
    adrp x1, kernel_begin
    add x1, x1, :lo12:kernel_begin
    adrp x2, _start
    add x2, x2, :lo12:_start
    mov x3, #(kernel_end - kernel_begin) & 0xffff
    movk x3, #((kernel_end - kernel_begin) >> 16) & 0xffff, lsl #16
2:  ldr x4, [x1], #8
    str x4, [x2], #8
    subs x3, x3, #8
    b.ne 2b
    dsb sy
    ic iallu
    dsb sy
    isb
    mov x0, x19
    mov x1, xzr
    mov x2, xzr
    mov x3, xzr
    adrp x4, _start
    add x4, x4, :lo12:_start
    br x4

    .balign 2048
probe_vectors:
    .rept 16
#if STAGE >= 16
#if STAGE >= 21
    b stage21_exception_entry
#elif STAGE == 20
    b stage20_exception_entry
#elif STAGE == 19
    b stage19_exception_entry
#elif STAGE == 18
    b stage18_exception_entry
#elif STAGE == 17
    b stage17_exception_entry
#else
    b stage16_exception_entry
#endif
#else
    b .
#endif
    .space 124
    .endr
#if STAGE == 16
stage16_exception_entry:
    /* No return from exceptions. Capture EL2 cause while USB is alive. */
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne 96f
    mrs x0, ESR_EL2
    mrs x1, ELR_EL2
    mrs x2, FAR_EL2
    b 97f
96:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
97:
    bl stage16_fault
95: wfe
    b 95b
#endif
#if STAGE == 17
stage17_exception_entry:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne 94f
    mrs x0, ESR_EL2
    mrs x1, ELR_EL2
    mrs x2, FAR_EL2
    b 93f
94:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
93:
    bl stage17_fault
92: wfe
    b 92b
#endif
#if STAGE == 18
stage18_exception_entry:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne 91f
    mrs x0, ESR_EL2
    mrs x1, ELR_EL2
    mrs x2, FAR_EL2
    b 90f
91:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
90:
    bl stage18_fault
89: wfe
    b 89b
#endif
#if STAGE == 19
stage19_exception_entry:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne 87f
    mrs x0, ESR_EL2
    mrs x1, ELR_EL2
    mrs x2, FAR_EL2
    b 86f
87:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
86:
    bl stage19_fault
85: wfe
    b 85b
#endif
#if STAGE >= 20
    /*
     * Called from stage19_probe after successful segment, ADT and
     * boot_args loading. The saved EL2 stack and return address let
     * HVC restore control without touching the Linux recovery kernel.
     * x0=real XNU entry PA, x1=boot_args PA; XNU is never branched to.
     */
    .global stage20_el1_transition
stage20_el1_transition:
    /* XNU's early boot writes x20-x24. AAPCS64 requires our EL2
     * trampoline to restore x19-x29 before returning to C. The
     * Stage20-D first hardware trace exposed x22 containing the
     * XNU virtBase (0xfffffff006000000) on our EL2 return. */
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!
    adr x9, stage20_el1_stack_top
    msr SP_EL1, x9
    adr x9, probe_vectors
    msr VBAR_EL1, x9
    movz x9, #0x0800
    movk x9, #0x30d0, lsl #16
    msr SCTLR_EL1, x9
    mov x9, #(1 << 31)
    msr HCR_EL2, x9
    msr VTTBR_EL2, xzr
    msr CPTR_EL2, xzr
    msr MDCR_EL2, xzr
    mov x9, #3
    msr CNTHCTL_EL2, x9
    msr CNTVOFF_EL2, xzr
    adr x9, stage20_at_el1
    msr ELR_EL2, x9
    mov x9, #0x3c5             /* masked interrupts, EL1h */
    msr SPSR_EL2, x9
    isb
    eret

stage20_at_el1:
#if STAGE >= 21
    /* Retain original entry/boot_args through the shim-owned log call. */
    mov x20, x0
    mov x21, x1
    bl stage20_el1_signal
    mov x0, x21               /* XNU ABI: x0 = physical boot_args */
    dsb sy
    isb
    br x20                    /* Actual first XNU instruction is a B */
#else
    bl stage20_el1_signal
    hvc #0x20
#endif
    /* Unexpected HVC return to EL1: never branch into XNU. */
stage20_unexpected_return:
    wfe
    b stage20_unexpected_return

stage20_resume_el2:
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret

stage20_exception_entry:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne stage20_unexpected_fault
    mrs x0, ESR_EL2
    ubfx x1, x0, #26, #6
    cmp x1, #0x16             /* AArch64 HVC exception */
    b.ne stage20_unexpected_fault
    and x1, x0, #0xffff
    cmp x1, #0x20             /* explicitly requested HVC #0x20 */
    b.ne stage20_unexpected_fault
    mrs x1, ELR_EL2
    mrs x2, SPSR_EL2
    bl stage20_hvc_signal
    adr x9, stage20_resume_el2
    msr ELR_EL2, x9
    mov x9, #0x3c9            /* masked interrupts, EL2h */
    msr SPSR_EL2, x9
    isb
    eret

#if STAGE >= 21
/* EL1 synchronous exception after the genuine XNU entry branch.
 * Every exception vector lands here, including lower-EL HVC to EL2. */
stage21_exception_entry:
#if STAGE >= 29
    /* Capture the original XNU registers before the HVC handler
     * clobbers x0/x1/x2/x9. The EL2 logger reads this physical buffer.
     * The vector stub may already have clobbered x16, so do not treat
     * it as a recovered original register. No virtual-stack reads. */
    adr x9, stage24_register_snapshot
    stp x0, x1, [x9, #0]
    stp x2, x3, [x9, #16]
    stp x4, x5, [x9, #32]
    stp x6, x7, [x9, #48]
    str x8, [x9, #64]
    stp x19, x20, [x9, #72]
    stp x21, x22, [x9, #88]
    stp x23, x24, [x9, #104]
    stp x25, x26, [x9, #120]
    stp x27, x28, [x9, #136]
    stp x29, x30, [x9, #152]
    mrs x10, SP_EL0
    str x10, [x9, #168]
    mrs x10, SP_EL1
    str x10, [x9, #176]
    dsb sy
#endif
    mrs x9, CurrentEL
    cmp x9, #4
    b.eq stage21_el1_exception
    cmp x9, #8
    b.ne stage20_unexpected_fault
    mrs x0, ESR_EL2
    ubfx x1, x0, #26, #6
    cmp x1, #0x16
    b.ne stage20_unexpected_fault
    and x1, x0, #0xffff
    cmp x1, #0x21
    b.ne stage20_unexpected_fault
    mrs x1, ELR_EL2
    mrs x2, SPSR_EL2
    bl stage20b_hvc_exception
    adr x9, stage20_resume_el2
    msr ELR_EL2, x9
    mov x9, #0x3c9
    msr SPSR_EL2, x9
    isb
    eret
    .global stage20d_el1_exception_entry
stage20d_el1_exception_entry:
stage21_el1_exception:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
    mov x3, x20               /* XNU preserved boot_args in x20 */
#if STAGE == 23
    mov x4, x22               /* actual XNU read of virtBase */
    mov x5, x23               /* actual XNU read of physBase */
    mov x6, x24               /* actual XNU read of memSize */
    mrs x7, VBAR_EL1         /* prove vector handoff address */
#endif
    bl stage20b_el1_exception
    hvc #0x21
    b stage20_unexpected_return
#if STAGE == 23
    /* ADR computes a load-address-correct PA for the vector trampoline. */
    .global stage20d_vector_handler_address
stage20d_vector_handler_address:
    adr x0, stage20d_el1_exception_entry
    ret
#endif
#endif

stage20_unexpected_fault:
    mrs x0, CurrentEL
    cmp x0, #8
    b.ne 84f
    mrs x0, ESR_EL2
    mrs x1, ELR_EL2
    mrs x2, FAR_EL2
    b 83f
84:
    mrs x0, ESR_EL1
    mrs x1, ELR_EL1
    mrs x2, FAR_EL1
83:
    bl stage19_fault
82: wfe
    b 82b

    .balign 16
stage20_el1_stack:
    .space 8192
stage20_el1_stack_top:
#endif
    .balign 16
probe_bss:
    .space 4096
probe_bss_end:
    .space 32768
probe_stack_top:
#if STAGE >= 29
    .balign 16
    .global stage24_register_snapshot
stage24_register_snapshot:
    .space 184
#endif
    .balign 4096
    .global image_end
image_end:
#if STAGE == 6
stage_6_message:
    .asciz "stage 6 transport"
#endif
