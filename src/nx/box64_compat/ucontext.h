/* Box64 on Switch: Linux AArch64 ucontext layout. No POSIX signals exist here;
   os_nx fills one from a libnx ThreadExceptionDump so Box64's fault paths can run. */
#pragma once
#include <stdint.h>
#include <signal.h>
typedef struct {
    uint64_t fault_address;
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
    uint8_t __reserved[4096] __attribute__((aligned(16)));
} mcontext_t;
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } nx_stack_t;
typedef struct ucontext {
    unsigned long uc_flags;
    struct ucontext *uc_link;
    nx_stack_t uc_stack;
    sigset_t uc_sigmask;
    mcontext_t uc_mcontext;
} ucontext_t;
/* Linux <asm/sigcontext.h> records stored in mcontext_t.__reserved. */
#define FPSIMD_MAGIC 0x46508001
struct _aarch64_ctx { uint32_t magic; uint32_t size; };
struct fpsimd_context {
    struct _aarch64_ctx head;
    uint32_t fpsr;
    uint32_t fpcr;
    __uint128_t vregs[32];
};
