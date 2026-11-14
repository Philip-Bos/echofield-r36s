/* Minimal glibc-compatible program entry for aarch64 (equivalent to Scrt1.o).
 * Calls __libc_start_main@GLIBC_2.17(main, argc, argv, init, fini, rtld_fini, stack_end).
 * init/fini are NULL: ECHOFIELD has no static constructors (checked by build.sh). */
    .text
    .globl _start
    .type _start, %function
_start:
    mov x29, #0
    mov x30, #0
    mov x5, x0              /* rtld_fini from the dynamic loader */
    ldr x1, [sp]            /* argc */
    add x2, sp, #8          /* argv */
    mov x6, sp              /* stack_end */
    adrp x0, :got:main
    ldr x0, [x0, #:got_lo12:main]
    mov x3, #0
    mov x4, #0
    bl __libc_start_main
    brk #0x3e8
    .size _start, .-_start
    .section .note.GNU-stack,"",%progbits
