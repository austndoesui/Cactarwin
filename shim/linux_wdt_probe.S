/* One-shot MTK top-RGU watchdog probe with TWRP recovery chainload.
 * WARNING: the retained marker's reset persistence is hardware-unverified.
 * Do not flash until its preservation by LK has been checked.
 */
    .section .text, "ax"
    .balign 8
    .global _start
_start:
    b probe
    .long 0
    .quad 0
    .quad chain_end - _start
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
    /* RGU_NONRST_REG2 holds the attempt marker on warm reset. */
    movz x9, #0x7000
    movk x9, #0x1000, lsl #16
    ldr w10, [x9, #0x24]
    movz w11, #0xc0de
    movk w11, #0xcac7, lsl #16
    cmp w10, w11
    b.eq recovered
    /* Save marker, then arm reset-mode top-RGU watchdog for 5 seconds. */
    str w11, [x9, #0x24]
    mov w12, #0x2808
    str w12, [x9, #4]
    mov w12, #0x1971
    str w12, [x9, #8]
    movz w12, #0x0005
    movk w12, #0x2200, lsl #16
    str w12, [x9]
    dsb sy
1:  wfe
    b 1b
recovered:
    /* Remove marker and disable top RGU before starting Linux. */
    str wzr, [x9, #0x24]
    movz w12, #0
    movk w12, #0x2200, lsl #16
    str w12, [x9]
    dsb sy
    b chainload
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
chain_end:
