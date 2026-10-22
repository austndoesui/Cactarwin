/* Cactus LK -> shim entry-state witness -> original TWRP Linux.
 * Isolates the register/stack/vector/BSS preparation used by start.S.
 * No watchdog, pstore, RTC, panel, or XNU access. Never enables the MMU.
 * A successful TWRP boot validates these steps together, not XNU entry.
 */
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
    msr vbar_el1, x1
    isb
    adr x1, probe_bss
    adr x2, probe_bss_end
1:  cmp x1, x2
    b.hs chainload
    str xzr, [x1], #8
    b 1b

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
    /* A synchronous fault stays in this safe loop. */
    .rept 16
    b .
    .space 124
    .endr
    .balign 16
probe_bss:
    .space 4096
probe_bss_end:
    .space 32768
probe_stack_top:
    .balign 4096
image_end:
