/* BrainCE Layer -- unimplemented-API stub pool.
 *
 * 256 slots, 8 bytes each: load the slot index into r0 and tail-branch
 * to ce_stub_common (lr still points at the CE caller, so the C
 * function's normal return goes straight back to CE code with the
 * stub's return value in r0). The loader binds every unimplemented
 * import to one of these slots; ce_stub_register() records which
 * dll/ordinal/name each slot stands for, and ce_stub_common logs the
 * call and returns 0 (failure) as DESIGN section 9.1 requires.
 */
    .text
    .align 2
    .global ce_stub_pool
    .type   ce_stub_pool, %function
ce_stub_pool:
    .set i, 0
    .rept 256
    mov r0, #i
    b   ce_stub_common
    .set i, i + 1
    .endr
    .size ce_stub_pool, . - ce_stub_pool
    .section .note.GNU-stack,"",%progbits
