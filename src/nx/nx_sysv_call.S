// Call an AAPCS64 host function with arguments taken from a SysV AMD64 guest frame.
// SysV and AAPCS64 count integer and vector argument registers independently:
//   rdi,rsi,rdx,rcx,r8,r9 -> x0..x5
//   stack_args[0..1]      -> x6..x7   (AAPCS64 integer args 7 & 8)
//   stack_args[2..]       -> [sp+0..] (16-byte aligned)
//   xmm0..7               -> q0..q7
// Not covered: struct returns > 16 bytes (SysV hidden pointer in rdi, AAPCS64 in x8).
//
// uint64_t nx_sysv_call(void *fn, const uint64_t gpr[6], const void *xmm /* 8 x 16 bytes */,
//                       const uint64_t *stack_args, uint64_t stack_count, void *q0_out /* 16 bytes */);
    .text
    .global nx_sysv_call
    .type nx_sysv_call, %function
    .balign 4
nx_sysv_call:
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    mov     x19, x0                 // fn
    mov     x20, x5                 // q0_out
    cmp     x4, #2
    b.ls    2f
    sub     x12, x4, #2             // stack_slots = stack_count - 2
    add     x9, x12, #1             // keep sp 16-byte aligned: round slots up to even
    and     x9, x9, #0xfffffffffffffffe
    lsl     x9, x9, #3
    sub     sp, sp, x9
    mov     x10, #0
1:  cmp     x10, x12
    b.hs    2f
    add     x13, x10, #2
    ldr     x11, [x3, x13, lsl #3]
    str     x11, [sp, x10, lsl #3]
    add     x10, x10, #1
    b       1b
2:  mov     x6, #0
    mov     x7, #0
    cmp     x4, #1
    b.lo    3f
    ldr     x6, [x3]
    cmp     x4, #2
    b.lo    3f
    ldr     x7, [x3, #8]
3:  ldp     q0, q1, [x2]
    ldp     q2, q3, [x2, #32]
    ldp     q4, q5, [x2, #64]
    ldp     q6, q7, [x2, #96]
    mov     x9, x1
    ldp     x0, x1, [x9]
    ldp     x2, x3, [x9, #16]
    ldp     x4, x5, [x9, #32]
    blr     x19
    str     q0, [x20]
    mov     sp, x29
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #32
    ret
    .size nx_sysv_call, . - nx_sysv_call
