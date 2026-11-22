// SPDX-License-Identifier: MIT
// Switch host glue for Box64 (third_party/box64): process init, per-thread emulators,
// SysV->AAPCS64 bridges, and the x64_exec.h API used by loader.c.
// Modeled on third_party/box64/wine/wow64/wowbox64.c.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <switch.h>

#include "x64_exec.h"

#include "debug.h"
#include "os.h"
#include "custommem.h"
#include "env.h"
#include "emu/x64emu_private.h"
#include "emu/x87emu_private.h"
#include "emu/x64run_private.h"
#include "x64emu.h"
#include "x64trace.h"
#include "box64context.h"
#include "box64cpu.h"
#include "build_info.h"
#include "rbtree.h"
#include "hostext.h"
#include "sysinfo.h"

typedef void (*wrapper_t)(x64emu_t *emu, uintptr_t fnc);

static box64context_t box64_context;
box64context_t *my_context = &box64_context;

/* ---- hooks the Box64 core expects from its host (same answers as wowbox64.c) ---- */
int is_addr_unaligned(uintptr_t addr) { (void)addr; return 0; }
int is_addr_autosmc(uintptr_t addr) { (void)addr; return 0; }
int nUnalignedRange(uintptr_t start, size_t size) { (void)start; (void)size; return 0; }
void getUnalignedRange(uintptr_t start, size_t size, uintptr_t addrs[]) { (void)start; (void)size; (void)addrs; }
/* 0 = no direct-call fast path: every bridge goes through EmuInt3 below. */
int isSimpleWrapper(wrapper_t fun) { (void)fun; return 0; }
int isRetX87Wrapper(wrapper_t fun) { (void)fun; return 0; }

/* ---- bridges: CC 'S' 'C' <wrapper:8> <fn:8> C3, padded to 32 bytes ---- */
enum { BRIDGE_SIZE = 32, BRIDGE_ARENA = 1024 * 1024 };
static uint8_t *bridge_arena;
static size_t bridge_used;
static Mutex bridge_lock;
static uint64_t exit_bridge;

static uint64_t make_bridge(uintptr_t wrapper, uintptr_t fn, const char *name) {
    mutexLock(&bridge_lock);
    if (!bridge_arena) bridge_arena = (uint8_t *)memalign(4096, BRIDGE_ARENA);
    if (!bridge_arena || bridge_used + BRIDGE_SIZE > BRIDGE_ARENA) {
        mutexUnlock(&bridge_lock);
        fprintf(stderr, "nx_x64: bridge arena exhausted\n");
        return 0;
    }
    uint8_t *b = bridge_arena + bridge_used;
    bridge_used += BRIDGE_SIZE;
    memset(b, 0xcc, BRIDGE_SIZE);
    b[0] = 0xcc; b[1] = 'S'; b[2] = 'C';
    memcpy(b + 3, &wrapper, 8);
    memcpy(b + 11, &fn, 8);
    b[19] = 0xc3;
    uintptr_t nptr = (uintptr_t)name;
    memcpy(b + 20, &nptr, 8);
    mutexUnlock(&bridge_lock);
    return (uint64_t)(uintptr_t)b;
}

uint64_t nx_sysv_call(void *fn, const uint64_t gpr[6], const void *xmm, const uint64_t *stack_args,
                      uint64_t stack_count, void *q0_out);

enum { SYSV_STACK_ARGS = 8 }; /* up to 14 integer arguments */

/* Runs with R_RSP at the guest's return address (the bridge was reached by `call`). */
static void sysv_wrapper(x64emu_t *emu, uintptr_t fn) {
    uint64_t gpr[6] = {R_RDI, R_RSI, R_RDX, R_RCX, R_R8, R_R9};
    uint8_t xmm[8][16], q0[16];
    for (int i = 0; i < 8; ++i) memcpy(xmm[i], &emu->xmm[i], 16);
    uint64_t rax = nx_sysv_call((void *)fn, gpr, xmm, (const uint64_t *)(uintptr_t)(R_RSP + 8), SYSV_STACK_ARGS, q0);
    R_RAX = rax;
    R_RDX = 0; /* no 128-bit integer returns from HLE functions */
    memcpy(&emu->xmm[0], q0, 16);
}

uint64_t nx_x64_bridge(void *host_fn) {
    return make_bridge((uintptr_t)sysv_wrapper, (uintptr_t)host_fn, NULL);
}

uint64_t nx_x64_bridge_named(void *host_fn, const char *name) {
    return make_bridge((uintptr_t)sysv_wrapper, (uintptr_t)host_fn, name);
}

int g_nx_trace_loop = 0;
uint64_t g_nx_image_base = 0;
__thread int g_nx_guest_worker_id = 0;

/* Stall watchdog (nx.ini STALL_DUMP=1): every guest thread owns a slot holding its current HLE call;
 * a host thread dumps all slots every 5 s, so a hang shows who waits on what and for how long. */
typedef struct {
    const char *thread, *hle, *last_hle;
    uint64_t a0, a1, a2, ret_ip, t_enter, last_ret, calls, calls_seen;
    volatile int in_call, alive;
    x64emu_t *emu; /* registers are exact with DYNAREC=0; the dynarec only syncs them at block exits */
} NxHleSlot;
static NxHleSlot g_hle_slots[96];
uint64_t nx_dyn_stat_block, nx_dyn_stat_interp; /* DynaRun: native blocks entered vs interpreter fallbacks */
uintptr_t nx_dyn_last_interp;
static __thread NxHleSlot *t_hle_slot;
static int g_stall_dump = -1;

static uint64_t nx_now_ms(void) { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }

static void *stall_dump_thread(void *unused) {
    (void)unused;
    extern void nx_platform_trace(const char *format, ...);
    for (;;) {
        svcSleepThread(5000000000LL);
        uint64_t now = nx_now_ms();
        static uint64_t seen_block, seen_interp;
        uint64_t nb = nx_dyn_stat_block, ni = nx_dyn_stat_interp;
        uint64_t li = nx_dyn_last_interp;
        nx_platform_trace("STALL dump t=%llums dynarec=%d blocks +%llu interp +%llu (last interp @0x%llx)\n",
                          (unsigned long long)now, (int)BOX64ENV(dynarec), (unsigned long long)(nb - seen_block),
                          (unsigned long long)(ni - seen_interp),
                          (unsigned long long)((g_nx_image_base && li >= g_nx_image_base) ? li - g_nx_image_base : li));
        seen_block = nb; seen_interp = ni;
        for (int i = 0; i < (int)(sizeof g_hle_slots / sizeof *g_hle_slots); ++i) {
            NxHleSlot *s = &g_hle_slots[i];
            if (!s->alive) continue;
            uint64_t calls = s->calls, delta = calls - s->calls_seen;
            s->calls_seen = calls;
            uint64_t rel = (g_nx_image_base && s->ret_ip >= g_nx_image_base) ? s->ret_ip - g_nx_image_base : s->ret_ip;
            if (s->in_call)
                nx_platform_trace("  [%d] %s: IN %s(0x%llx, 0x%llx, 0x%llx) for %llums @0x%llx, +%llu calls\n", i,
                                  s->thread, s->hle, (unsigned long long)s->a0, (unsigned long long)s->a1,
                                  (unsigned long long)s->a2, (unsigned long long)(now - s->t_enter),
                                  (unsigned long long)rel, (unsigned long long)delta);
            else
                nx_platform_trace("  [%d] %s: guest code, +%llu calls, last %s -> 0x%llx @0x%llx\n", i, s->thread,
                                  (unsigned long long)delta, s->last_hle ? s->last_hle : "-",
                                  (unsigned long long)s->last_ret, (unsigned long long)rel);
            /* A thread in guest code with no HLE calls: sample where it is, to tell a loop from work. */
            if (!s->in_call && !delta && s->emu) {
                x64emu_t *emu = s->emu;
                for (int k = 0; k < 6; ++k) {
                    uint64_t rip = R_RIP;
                    uint64_t r = (g_nx_image_base && rip >= g_nx_image_base) ? rip - g_nx_image_base : rip;
                    nx_platform_trace("      rip=0x%llx rax=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx r8=%llx r9=%llx\n",
                                      (unsigned long long)r, (unsigned long long)R_RAX, (unsigned long long)R_RCX,
                                      (unsigned long long)R_RDX, (unsigned long long)R_RSI, (unsigned long long)R_RDI,
                                      (unsigned long long)R_R8, (unsigned long long)R_R9);
                    svcSleepThread(30000000LL);
                }
            }
        }
    }
    return NULL;
}

void nx_hle_thread_begin(const char *name) {
    if (g_stall_dump < 0) {
        const char *e = getenv("DCP_STALL_DUMP");
        g_stall_dump = (e && *e && strcmp(e, "0")) ? 1 : 0;
        if (g_stall_dump) {
            pthread_t th;
            pthread_create(&th, NULL, stall_dump_thread, NULL);
        }
    }
    if (!g_stall_dump) return;
    mutexLock(&bridge_lock);
    for (int i = 0; i < (int)(sizeof g_hle_slots / sizeof *g_hle_slots); ++i) {
        if (g_hle_slots[i].alive) continue;
        memset(&g_hle_slots[i], 0, sizeof g_hle_slots[i]);
        g_hle_slots[i].thread = name ? name : "?";
        g_hle_slots[i].alive = 1;
        t_hle_slot = &g_hle_slots[i];
        break;
    }
    mutexUnlock(&bridge_lock);
}

void nx_hle_thread_end(void) {
    if (t_hle_slot) t_hle_slot->alive = 0;
    t_hle_slot = NULL;
}

/* Interpreter INT3 and the dynarec's generic bridge path both land here. *addr = address after the CC. */
void EmuInt3(void *opaque, void *a) {
    x64emu_t *emu = (x64emu_t *)opaque;
    uintptr_t *addr = (uintptr_t *)a;
    const uint8_t *b = (const uint8_t *)(*addr - 1);
    if (IsBridgeSignature(b[1], b[2])) {
        uintptr_t w, fn, nptr = 0;
        memcpy(&w, b + 3, 8);
        if (!w) {
            R_RIP = (uintptr_t)(b + 11);
            emu->quit = 1;
            return;
        }
        memcpy(&fn, b + 11, 8);
        memcpy(&nptr, b + 20, 8);
        static int trace_hle = -1;
        if (trace_hle < 0) {
            const char *e = getenv("DCP_TRACE_HLE");
            trace_hle = (e && *e && strcmp(e, "0")) ? 1 : 0;
        }
        if (trace_hle && nptr) {
            const char *nm = (const char *)nptr;
            if (strncmp(nm, "scePthreadMutex", 15) && strncmp(nm, "pthread_", 8) &&
                strcmp(nm, "sceKernelUsleep") && strcmp(nm, "sceKernelGetProcessTimeCounter") &&
                strcmp(nm, "sceKernelGetProcessTime") && strcmp(nm, "sceKernelReadTsc") &&
                strcmp(nm, "memcpy") && strcmp(nm, "memset") && strcmp(nm, "memmove") &&
                strcmp(nm, "memcmp") && strcmp(nm, "strlen") && strcmp(nm, "malloc") &&
                strcmp(nm, "free") && strcmp(nm, "calloc") && strcmp(nm, "realloc")) {
                static const char *seen[256];
                static int seen_count = 0;
                mutexLock(&bridge_lock);
                int found = 0;
                for (int i = 0; i < seen_count; ++i) {
                    if (seen[i] == nm) { found = 1; break; }
                }
                if (!found && seen_count < 256) {
                    seen[seen_count++] = nm;
                    int idx = seen_count;
                    mutexUnlock(&bridge_lock);
                    extern void nx_platform_trace(const char *format, ...);
                    nx_platform_trace("HLE[%d]: %s (rdi=0x%llx)\n", idx, nm, (unsigned long long)R_RDI);
                } else {
                    mutexUnlock(&bridge_lock);
                }
            }
        }
        RESET_FLAGS(emu);
        R_RIP = (uintptr_t)(b + 19);
        uint64_t arg0 = R_RDI, arg1 = R_RSI, arg2 = R_RDX;
        uint64_t ret_ip = R_RSP ? *(uint64_t *)(uintptr_t)R_RSP : 0;
        NxHleSlot *slot = t_hle_slot;
        if (slot) {
            slot->emu = emu;
            slot->hle = nptr ? (const char *)nptr : "?";
            slot->a0 = arg0; slot->a1 = arg1; slot->a2 = arg2; slot->ret_ip = ret_ip;
            slot->t_enter = nx_now_ms();
            slot->in_call = 1;
        }
        ((wrapper_t)w)(emu, fn);
        if (slot) {
            slot->in_call = 0;
            slot->last_hle = slot->hle;
            slot->last_ret = R_RAX;
            ++slot->calls;
        }
        if (g_nx_guest_worker_id > 0 && nptr) {
            const char *nm = (const char *)nptr;
            if (strcmp(nm, "memcpy") && strcmp(nm, "memset") && strcmp(nm, "memmove") &&
                strcmp(nm, "memcmp") && strcmp(nm, "strlen") && strcmp(nm, "malloc") &&
                strcmp(nm, "free") && strcmp(nm, "calloc") && strcmp(nm, "realloc") &&
                strncmp(nm, "scePthreadMutex", 15) &&
                strcmp(nm, "sceKernelGetProcessTimeCounter") && strcmp(nm, "sceKernelGetProcessTime") &&
                strcmp(nm, "sceKernelReadTsc") && strcmp(nm, "sceKernelUsleep")) {
                static int w_count = 0;
                int c = __atomic_add_fetch(&w_count, 1, __ATOMIC_RELAXED);
                if (c <= 150) {
                    extern void nx_platform_trace(const char *format, ...);
                    nx_platform_trace("W%d[%d]: %s(0x%llx, 0x%llx, 0x%llx) -> 0x%llx\n",
                                      g_nx_guest_worker_id, c, nm,
                                      (unsigned long long)arg0, (unsigned long long)arg1,
                                      (unsigned long long)arg2, (unsigned long long)R_RAX);
                }
            }
        }
        if (g_nx_trace_loop && nptr) {
            const char *nm = (const char *)nptr;
            if (strcmp(nm, "memcpy") && strcmp(nm, "memset") && strcmp(nm, "memmove") &&
                strcmp(nm, "memcmp") && strcmp(nm, "strlen") && strcmp(nm, "malloc") &&
                strcmp(nm, "free") && strcmp(nm, "calloc") && strcmp(nm, "realloc") &&
                strcmp(nm, "sceKernelGetProcessTimeCounter") && strcmp(nm, "sceKernelGetProcessTime") &&
                strcmp(nm, "sceKernelReadTsc") && strcmp(nm, "sceKernelUsleep")) {
                static int loop_count = 0;
                mutexLock(&bridge_lock);
                int c = ++loop_count;
                mutexUnlock(&bridge_lock);
                if (c <= 80) {
                    extern void nx_platform_trace(const char *format, ...);
                    uint64_t rel = (g_nx_image_base && ret_ip >= g_nx_image_base) ? (ret_ip - g_nx_image_base) : ret_ip;
                    nx_platform_trace("L[%d|%llu|@0x%llx]: %s(0x%llx, 0x%llx) -> 0x%llx\n",
                                      c, (unsigned long long)(uintptr_t)emu, (unsigned long long)rel, nm,
                                      (unsigned long long)arg0, (unsigned long long)arg1,
                                      (unsigned long long)R_RAX);
                    if (!strcmp(nm, "sceUserServiceGetEvent")) {
                        static int walked = 0;
                        if (!walked) {
                            walked = 1;
                            const uint64_t *sp = (const uint64_t *)(uintptr_t)R_RSP;
                            for (int i = 0; i < 96; ++i) {
                                uint64_t v = sp[i];
                                if (g_nx_image_base && v >= g_nx_image_base && v < g_nx_image_base + 0x1200000ULL) {
                                    nx_platform_trace("  STK[+%d]: 0x%llx\n", i * 8, (unsigned long long)(v - g_nx_image_base));
                                }
                            }
                        }
                    }
                }
            }
        }
        if (trace_hle && nptr && ((int32_t)R_RAX < 0) && ((R_RAX >> 32) == 0 || (R_RAX >> 32) == 0xffffffffULL)) {
            const char *nm = (const char *)nptr;
            if (strcmp(nm, "sceUserServiceGetEvent") && strcmp(nm, "sceKernelPollEventFlag") &&
                strcmp(nm, "sceKernelLseek") && strcmp(nm, "lseek") &&
                strcmp(nm, "memset") && strcmp(nm, "memcpy") && strcmp(nm, "memmove") &&
                strcmp(nm, "malloc") && strcmp(nm, "calloc") && strcmp(nm, "realloc")) {
                static struct { const char *nm; uint32_t err; } errs[128];
                static int err_count = 0;
                uint32_t err = (uint32_t)R_RAX;
                mutexLock(&bridge_lock);
                int found = 0;
                for (int i = 0; i < err_count; ++i) {
                    if (errs[i].nm == nm && errs[i].err == err) { found = 1; break; }
                }
                if (!found && err_count < 128) {
                    errs[err_count].nm = nm;
                    errs[err_count].err = err;
                    ++err_count;
                    mutexUnlock(&bridge_lock);
                    extern void nx_platform_trace(const char *format, ...);
                    nx_platform_trace("HLE_ERR: %s -> 0x%x\n", nm, err);
                } else {
                    mutexUnlock(&bridge_lock);
                }
            }
        }
        return;
    }
    fprintf(stderr, "nx_x64: unhandled guest INT3 at %p\n", (void *)(*addr - 1));
    fflush(stderr);
    extern void runtime_log_flush(void);
    runtime_log_flush();
    emu->quit = 1;
    _exit(24); /* a silent return would make the caller believe the guest function finished */
}

int IsBridgeSignature(char s, char c) { return s == 'S' && c == 'C'; }

/* ---- process / thread setup ---- */
static int box64_ready;

static void box64_process_init(void) {
    if (box64_ready) return;
    /* ARM64 hardware has weak memory ordering (unlike x86/Ryujinx TSO); default strongmem=1 unless overridden. */
    setenv("BOX64_DYNAREC_STRONGMEM", "1", 0);
    LoadEnvVariables();
    InitializeEnvFiles();
    InitializeSystemInfo();
    if (!BOX64ENV(nobanner)) PrintBox64Version(1);
    if (DetectHostCpuFeatures())
        PrintHostCpuFeatures();
    else {
        printf_log(LOG_INFO, "Minimum CPU requirements not met, disabling DynaRec\n");
        SET_BOX64ENV(dynarec, 0);
    }
    const char *dyn_env = getenv("DCP_DYNAREC");
    if (dyn_env && !strcmp(dyn_env, "0")) {
        SET_BOX64ENV(dynarec, 0);
    } else {
        SET_BOX64ENV(dynarec, 1);
    }
    PrintEnvVariables(&box64env, LOG_INFO);
    init_custommem_helper(&box64_context);
    box64_context.db_sizes = rbtree_init("db_sizes");
    InitX64Trace(&box64_context);
    exit_bridge = make_bridge(0, 0, "exit");
    box64_ready = 1;
}

/* Host->guest calls nest (guest -> HLE -> guest callback), so each level gets its own emulator.
   A fiber brings its own array: its calls run while the thread that started it is suspended. */
enum { MAX_NEST = 32 };
struct NxX64FiberEmus {
    x64emu_t *emus[MAX_NEST];
    int depth;
};
static __thread x64emu_t *thread_emus[MAX_NEST];
static __thread int thread_depth;
static __thread NxX64FiberEmus *fiber_emus;
static __thread uint64_t thread_tls_base;

void nx_x64_set_tls_base(uint64_t base) { thread_tls_base = base; }

NxX64FiberEmus *nx_x64_fiber_emus_new(void) { return (NxX64FiberEmus *)calloc(1, sizeof(NxX64FiberEmus)); }

void nx_x64_fiber_emus_free(NxX64FiberEmus *emus) {
    if (!emus) return;
    for (int i = 0; i < MAX_NEST; ++i)
        if (emus->emus[i]) FreeX64Emu(&emus->emus[i]);
    free(emus);
}

void nx_x64_fiber_emus_activate(NxX64FiberEmus *emus) { fiber_emus = emus; }

static x64emu_t **emu_slots(void) { return fiber_emus ? fiber_emus->emus : thread_emus; }
static int *emu_depth(void) { return fiber_emus ? &fiber_emus->depth : &thread_depth; }

struct NxX64Context {
    void *mem_base;
    size_t mem_size;
};

NxX64Context *nx_x64_create(void *mem_base, size_t mem_size) {
    box64_process_init();
    NxX64Context *ctx = (NxX64Context *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->mem_base = mem_base;
    ctx->mem_size = mem_size;
    return ctx;
}

/* The dynarec only builds blocks for memory its memprot map knows as PROT_READ|PROT_EXEC. On Linux
 * Box64's mmap wrappers fill that map; here the loader maps the image itself, so without this every
 * block is refused and DynaRun silently interprets everything. mprotect is a no-op on Switch, so
 * Box64 cannot detect later writes to these pages: only code that is final at load time belongs here. */
void nx_x64_mark_code(NxX64Context *ctx, void *addr, size_t size) {
    (void)ctx;
    setProtection_elf((uintptr_t)addr, size, PROT_READ | PROT_EXEC);
}

void nx_x64_destroy(NxX64Context *ctx) { free(ctx); }

void nx_x64_register_import(NxX64Context *ctx, uint64_t guest_stub, const char *name, void *host_fn) {
    (void)ctx; (void)guest_stub; (void)name; (void)host_fn; /* imports are bound with nx_x64_bridge */
}

uint64_t nx_x64_call(NxX64Context *ctx, uint64_t entry_rip, uint64_t rsp, uint64_t fs_base,
                     uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx) {
    (void)ctx;
    box64_process_init();
    x64emu_t **slots = emu_slots();
    int *depth = emu_depth();
    if (*depth >= MAX_NEST) {
        fprintf(stderr, "nx_x64_call: guest/host nesting deeper than %d\n", MAX_NEST);
        return 0;
    }
    x64emu_t **slot = &slots[*depth];
    if (!*slot) *slot = NewX64Emu(&box64_context, 0, 0, 0, 0);
    x64emu_t *emu = *slot;
    if (!rsp) {
        if (*depth > 0) {
            rsp = slots[*depth - 1]->regs[_SP].q[0] - 256; /* skip the SysV red zone */
        } else {
            enum { FALLBACK_STACK = 512 * 1024 };
            static __thread uint8_t *fallback;
            if (!fallback) fallback = (uint8_t *)memalign(16, FALLBACK_STACK);
            if (!fallback) {
                fprintf(stderr, "nx_x64_call: no guest stack\n");
                return 0;
            }
            rsp = (uint64_t)(uintptr_t)(fallback + FALLBACK_STACK);
        }
    }
    ++*depth;
    rsp &= ~(uint64_t)15;
    rsp -= 8;
    *(uint64_t *)(uintptr_t)rsp = exit_bridge; /* `ret` from the guest function lands on the exit bridge */
    R_RSP = rsp;
    R_RIP = entry_rip;
    R_RDI = rdi; R_RSI = rsi; R_RDX = rdx; R_RCX = rcx;
    /* PS4 code addresses its TCB through fs:; the loader rewrote those to gs:. Point both at it. */
    uint64_t tls = fs_base ? fs_base : thread_tls_base;
    emu->segs[_FS] = emu->segs[_GS] = 0; /* 0 = use segs_offs as-is (GetSegmentBaseEmu) */
    emu->segs_offs[_FS] = emu->segs_offs[_GS] = tls;
    emu->quit = 0;
    if (BOX64ENV(dynarec)) DynaRun(emu);
    else Run(emu, 0);
    emu->quit = 0;
    --*depth;
    return R_RAX;
}
