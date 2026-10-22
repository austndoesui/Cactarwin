/* AArch64 Linux Image entry which moves the untouched recovery Image
 * down by 64 bytes, then enters it with the original LK DTB register.
 * The copy loop is after the embedded kernel, outside its destination.
 */
    .section .text, "ax"
    .balign 8
    .global _start
_start:
    b chainload
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
chainload:
    msr daifset, #0xf
    mov x19, x0                 /* LK-supplied FDT pointer */
    adrp x1, kernel_begin
    add x1, x1, :lo12:kernel_begin
    adrp x2, _start
    add x2, x2, :lo12:_start
    mov x3, #(kernel_end - kernel_begin) & 0xffff
    movk x3, #((kernel_end - kernel_begin) >> 16) & 0xffff, lsl #16
1:
    ldr x4, [x1], #8
    str x4, [x2], #8
    subs x3, x3, #8
    b.ne 1b
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
