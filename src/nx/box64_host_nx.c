// SPDX-License-Identifier: MIT
// Switch implementations of the OS services Box64's core links against (the Linux build gets
// them from core.c / os_linux.c / emit_signals_linux.c / hostext_linux.c, none of which apply).
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <switch.h>

#include "debug.h"
#include "os.h"
#include "box64context.h"
#include "x64emu.h"
#include "emu/x64emu_private.h"
#include "hostext.h"
#include "sysinfo.h"
#include "regs.h"
#include "elfloader.h"

/* ---- globals normally defined by core.c (values as in wowbox64.c) ---- */
uintptr_t box64_pagesize = 4096;
uint32_t default_gs = 0x2b;
uint32_t default_fs = 0;
int box64_rdtsc = 0;
uint8_t box64_rdtsc_shift = 0;
int box64_is32bits = 0;
int box64_unittest_mode = 0;
int box64_wine = 0;
int box64_mapclean = 0;
int box64_isAddressSpace32 = 0;
int box64_in_fatal_handler = 0;
int box64_cycle_log_initialized = 0;
char *ftrace_name = NULL;
sysinfo_t box64_sysinfo = {0};
cpu_ext_t cpuext = {0};

/* ---- CPU features: Tegra X1 Cortex-A57, ARMv8.0 + crypto + crc, no LSE atomics ---- */
int DetectHostCpuFeatures(void) {
    cpuext.asimd = 1;
    cpuext.aes = cpuext.sha1 = cpuext.sha2 = cpuext.pmull = 1;
    cpuext.crc32 = 1;
    cpuext.atomics = 0;
    return 1;
}

/* ---- allocation ---- */
void *__libc_malloc(size_t n) { return malloc(n); }
void *__libc_calloc(size_t n, size_t s) { return calloc(n, s); }
void *__libc_realloc(void *p, size_t n) { return realloc(p, n); }
void __libc_free(void *p) { free(p); }
char *box_strdup(const char *s) { return strdup(s); }

/* Anonymous mappings only. Data mappings come from the newlib heap (page aligned, zeroed);
   PROT_EXEC mappings come from a single libnx Jit CodeMemory pool (simultaneous RW + RX aliases). */
static Jit g_jit;
static int g_jit_ready;
static size_t g_jit_used;
static Mutex g_jit_lock;

static int ensure_jit(void) {
    if (g_jit_ready) return g_jit_ready > 0 ? 0 : -1;
    const char *env = getenv("DCP_NX_JIT_MB");
    size_t mb = env ? (size_t)strtoull(env, NULL, 10) : 48;
    if (mb < 16 || mb > 256) mb = 48;
    size_t size = mb * 1024 * 1024;
    Result rc = jitCreate(&g_jit, size);
    if (R_SUCCEEDED(rc)) rc = jitTransitionToExecutable(&g_jit);
    if (R_FAILED(rc)) {
        g_jit_ready = -1;
        fprintf(stderr, "nx_jit: jitCreate/Transition(%zu MiB) failed: 0x%x\n", mb, (unsigned)rc);
        return -1;
    }
    g_jit_ready = 1;
    return 0;
}

void *nx_jit_rw(void *p) {
    if (!p || g_jit_ready <= 0) return p;
    uintptr_t a = (uintptr_t)p;
    uintptr_t rx = (uintptr_t)g_jit.rx_addr;
    if (a >= rx && a < rx + g_jit.size) {
        return (void *)((uintptr_t)g_jit.rw_addr + (a - rx));
    }
    return p;
}

void *nx_jit_rx(void *p) {
    if (!p || g_jit_ready <= 0) return p;
    uintptr_t a = (uintptr_t)p;
    uintptr_t rw = (uintptr_t)g_jit.rw_addr;
    if (a >= rw && a < rw + g_jit.size) {
        return (void *)((uintptr_t)g_jit.rx_addr + (a - rw));
    }
    return p;
}

void nx_jit_flush(void *rx_start, size_t len) {
    if (!rx_start || !len) return;
    armDCacheFlush(nx_jit_rw(rx_start), len);
    armICacheInvalidate(rx_start, len);
}

void *InternalMmap(void *addr, unsigned long length, int prot, int flags, int fd, ssize_t offset) {
    (void)offset;
    if (fd != -1 || ((flags & MAP_FIXED) && addr)) {
        errno = ENOSYS;
        return MAP_FAILED;
    }
    if (prot & PROT_EXEC) {
        mutexLock(&g_jit_lock);
        if (ensure_jit() != 0) {
            mutexUnlock(&g_jit_lock);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        size_t aligned = (length + box64_pagesize - 1) & ~(box64_pagesize - 1);
        if (g_jit_used + aligned > g_jit.size) {
            mutexUnlock(&g_jit_lock);
            errno = ENOMEM;
            return MAP_FAILED;
        }
        void *rx = (uint8_t *)g_jit.rx_addr + g_jit_used;
        void *rw = (uint8_t *)g_jit.rw_addr + g_jit_used;
        g_jit_used += aligned;
        mutexUnlock(&g_jit_lock);
        memset(rw, 0, aligned);
        return rx;
    }
    void *p = memalign(box64_pagesize, length);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(p, 0, length);
    return p;
}
int InternalMunmap(void *addr, unsigned long length) {
    (void)length;
    if (g_jit_ready > 0) {
        uintptr_t a = (uintptr_t)addr;
        uintptr_t rx = (uintptr_t)g_jit.rx_addr;
        uintptr_t rw = (uintptr_t)g_jit.rw_addr;
        if ((a >= rx && a < rx + g_jit.size) || (a >= rw && a < rw + g_jit.size)) {
            return 0;
        }
    }
    free(addr); /* Box64 unmaps whole chunks it mapped */
    return 0;
}
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return InternalMmap(addr, length, prot, flags, fd, offset);
}
int munmap(void *addr, size_t length) { return InternalMunmap(addr, length); }
int mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return 0; }
int madvise(void *addr, size_t len, int advice) { (void)addr; (void)len; (void)advice; return 0; }
int shm_open(const char *name, int oflag, mode_t mode) { (void)name; (void)oflag; (void)mode; errno = ENOSYS; return -1; }
int shm_unlink(const char *name) { (void)name; errno = ENOSYS; return -1; }
long syscall(long number, ...) { (void)number; errno = ENOSYS; return -1; }

/* Loaded guest code segments registered by nx_x64_mark_code so BIGBLOCK=2 joins blocks across branches. */
enum { NX_CODE_RANGE_MAX = 32 };
static struct { uintptr_t start, end; } g_code_ranges[NX_CODE_RANGE_MAX];
static int g_code_range_count;
void nx_x64_register_code_range(uintptr_t start, size_t size) {
    if (!size) return;
    int idx = __atomic_fetch_add(&g_code_range_count, 1, __ATOMIC_RELAXED);
    if (idx < NX_CODE_RANGE_MAX) {
        g_code_ranges[idx].start = start;
        g_code_ranges[idx].end = start + size;
    }
}
int IsAddrElfOrFileMapped(uintptr_t addr) {
    int n = __atomic_load_n(&g_code_range_count, __ATOMIC_ACQUIRE);
    if (n > NX_CODE_RANGE_MAX) n = NX_CODE_RANGE_MAX;
    for (int i = 0; i < n; ++i) {
        if (addr >= g_code_ranges[i].start && addr < g_code_ranges[i].end) return 1;
    }
    return 0;
}

/* Box64 brackets allocator updates with these to stay consistent if a signal lands inside. */
static RMutex critical;
void enter_critical_section() { rmutexLock(&critical); }
void leave_critical_section() { rmutexUnlock(&critical); }

/* ---- threads, time, randomness ---- */
int GetTID(void) {
    u64 id = 0;
    svcGetThreadId(&id, CUR_THREAD_HANDLE);
    return (int)id;
}
int SchedYield(void) { svcSleepThread(0); return 0; }
uint64_t ReadTSC(x64emu_t *emu) { (void)emu; return armGetSystemTick(); }
uint32_t get_random32(void) { uint32_t v; randomGet(&v, sizeof v); return v; }
uint64_t get_random64(void) { uint64_t v; randomGet(&v, sizeof v); return v; }

/* ---- environment, files, logging ---- */
void *GetEnv(const char *name) { return getenv(name); }
int FileExist(const char *filename, int flags) {
    struct stat sb;
    if (stat(filename, &sb) == -1) return 0;
    if (flags == -1) return 1;
    if (flags & IS_FILE) return S_ISREG(sb.st_mode);
    return S_ISDIR(sb.st_mode);
}
int MakeDir(const char *folder) { return !mkdir(folder, 0777) || errno == EEXIST; }
size_t FileSize(const char *filename) {
    struct stat sb;
    if (stat(filename, &sb) == -1 || !S_ISREG(sb.st_mode)) return 0;
    return sb.st_size;
}
void PrintfFtrace(int prefix, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (prefix) fprintf(stderr, "[BOX64] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
int PrintFunctionAddr(uintptr_t nextaddr, const char *text) { (void)nextaddr; (void)text; return 0; }

/* ---- features Box64 only uses for Linux / Wine guests ---- */
void initCycleLog(box64context_t *context) { (void)context; }
void AddNewLibs(const char *list) { (void)list; }
void AppendList(path_collection_t *collection, const char *List, int folder) { (void)collection; (void)List; (void)folder; }
void DetectUnityPlayer(char *filename) { (void)filename; }
void ParseVolatileMetadata(char *filename, void *addr) { (void)filename; (void)addr; }
void add_unaligned_address(uintptr_t addr) { (void)addr; }
uintptr_t AddAltJump(bridge_t *bridge, uintptr_t addr, uintptr_t jump_to_addr) { (void)bridge; (void)addr; (void)jump_to_addr; return 0; }
void *GetSegmentBase(void *emu, uint32_t desc) { (void)emu; (void)desc; return NULL; }
void *EmuFork(void *emu, int forktype) { (void)emu; (void)forktype; return NULL; }

/* ---- guest faults and system calls: report and stop the emulator ---- */
extern void runtime_log_flush(void);
static void guest_stop(x64emu_t *emu, const char *what, int num, void *addr) {
    fprintf(stderr, "nx_x64: %s %d at %p (RIP=%p)\n", what, num, addr, (void *)R_RIP);
    fflush(stderr);
    fflush(stdout);
    runtime_log_flush();
    (void)emu;
    _exit(25); /* returning would make the caller believe the guest function succeeded */
}
void EmitSignal(x64emu_t *emu, int sig, void *addr, int code) { (void)code; guest_stop(emu, "guest signal", sig, addr); }
void EmitDiv0(x64emu_t *emu, void *addr, int code) { guest_stop(emu, "guest divide error", code, addr); }
void EmitInterruption(x64emu_t *emu, int num, void *addr) { guest_stop(emu, "guest interrupt", num, addr); }
void EmitWineInt(x64emu_t *emu, int num, void *addr) { guest_stop(emu, "guest wine int", num, addr); }
void CheckExec(x64emu_t *emu, uintptr_t addr) { (void)emu; (void)addr; }
void EmuX64Syscall(void *emu) { guest_stop((x64emu_t *)emu, "guest syscall", 0, NULL); }
void EmuX64Syscall_linux(void *emu) { EmuX64Syscall(emu); }
void EmuX86Syscall(void *emu) { EmuX64Syscall(emu); }
void ShowNativeBT(int log_minimum) { (void)log_minimum; } /* no execinfo/backtrace on newlib */

/* ---- symbol lookup: the guest is a PS4 image, not ELF objects Box64 loaded ---- */
elfheader_t *FindElfAddress(box64context_t *context, uintptr_t addr) { (void)context; (void)addr; return NULL; }
const char *GetBridgeName(void *p) { (void)p; return NULL; }
void *GetNativeFnc(uintptr_t fnc) { (void)fnc; return NULL; }
const char *GetNativeName(void *p, int lib) { (void)p; (void)lib; return "???"; }
const char *getAddrFunctionName(uintptr_t addr) { (void)addr; return "???"; }
/* 0: the dynarec translates calls into bridges as normal calls; the bridge's INT3 does the work. */
int IsNativeCall(uintptr_t addr, int is32bits, uintptr_t *calladdress, uint16_t *retn) {
    (void)addr; (void)is32bits; (void)calladdress; (void)retn;
    return 0;
}
void TBBTSOScanMainElf(void) {}
/* sysinfo.c probes `lscpu`; there is no shell. */
FILE *popen(const char *command, const char *type) { (void)command; (void)type; errno = ENOSYS; return NULL; }
int pclose(FILE *stream) { (void)stream; return -1; }
