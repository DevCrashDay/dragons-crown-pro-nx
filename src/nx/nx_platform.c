#include "nx_platform.h"
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __SWITCH__
#include <pthread.h>
#include <switch.h>
#endif

static int g_nx_initialized = 0;
static int g_nxlink_sock = -1;

static void ensure_dir(const char *path) {
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') ++s;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    return s;
}

static void nx_apply_env_kv(const char *key, const char *value) {
    char name[160];
    snprintf(name, sizeof name, "DCP_%s", key);
    setenv(name, value ? value : "", 1);
    /* Box64 and Mesa/Nouveau read their own unprefixed env names. */
    if (!strncmp(key, "BOX64_", 6) || !strncmp(key, "MESA_", 5) ||
        !strncmp(key, "NOUVEAU_", 8) || !strncmp(key, "LIBGL_", 6) ||
        !strcmp(key, "mesa_glthread")) {
        setenv(key, value ? value : "", 1);
    }
}

/* nx.ini: KEY=VALUE lines become DCP_KEY so the port's existing environment knobs work on a
 * console with no shell (AUDIO=none, PAD_MAP=..., NX_SAFE=0, NX_DMEM_MB=1536, ...). */
static void load_ini(void) {
    FILE *f = fopen("sdmc:/switch/dragons_crown_pro/nx.ini", "r");
    if (!f) return;
    char line[256];
    unsigned count = 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line), *value = trim(eq + 1);
        if (!*key || *key == '#' || *key == ';') continue;
        nx_apply_env_kv(key, value);
        ++count;
    }
    fclose(f);
    if (count) printf("Switch: %u settings from nx.ini\n", count);
}

int nx_platform_save_ini(const char *key, const char *value) {
    if (!key || !*key) return 0;
    const char *val = value ? value : "";
    nx_apply_env_kv(key, val);
#ifdef __SWITCH__
    static const char kIniPath[] = "sdmc:/switch/dragons_crown_pro/nx.ini";
    static const char kTmpPath[] = "sdmc:/switch/dragons_crown_pro/nx.ini.tmp";
    FILE *in = fopen(kIniPath, "r");
    FILE *out = fopen(kTmpPath, "w");
    if (!out) {
        if (in) fclose(in);
        return 0;
    }
    int replaced = 0;
    char line[512];
    if (in) {
        while (fgets(line, sizeof line, in)) {
            char probe[512];
            snprintf(probe, sizeof probe, "%s", line);
            char *eq = strchr(probe, '=');
            if (eq) {
                *eq = '\0';
                char *k = trim(probe);
                if (*k && *k != '#' && *k != ';' && strcmp(k, key) == 0) {
                    if (!replaced && *val) fprintf(out, "%s=%s\n", key, val);
                    replaced = 1;
                    continue;
                }
            }
            fputs(line, out);
        }
        fclose(in);
    }
    if (!replaced && *val) fprintf(out, "%s=%s\n", key, val);
    int ok = fflush(out) == 0 && !ferror(out);
    if (fclose(out) != 0) ok = 0;
    if (!ok) {
        remove(kTmpPath);
        return 0;
    }
    remove(kIniPath);
    if (rename(kTmpPath, kIniPath) != 0) {
        remove(kTmpPath);
        return 0;
    }
    nx_platform_trace("Switch: nx.ini %s=%s\n", key, *val ? val : "(unset)");
#else
    (void)val;
#endif
    return 1;
}

/* libnx pthreads inherit the creator's priority and the process default core, so every guest
 * thread lands on one core at one priority. Horizon never time-slices equal priorities (only
 * priority 59 on cores 0-2 is rotated every 10 ms), so one polling guest thread starves the rest.
 * NX_SCHED: off = libnx default; spread = priority 59 on cores 0-2; mapped = PS4 256..767 -> 44..59
 * on cores 0-2. Always logs where the thread runs. */
static void nx_cpu_register(const char *name);

void nx_platform_thread_setup(const char *name, int ps4_prio) {
#ifdef __SWITCH__
    static int next_index;
    const char *mode = getenv("DCP_NX_SCHED");
    if (!mode || !*mode) mode = "spread";
    int core0 = (int)svcGetCurrentProcessorNumber();
    s32 prio0 = -1;
    svcGetThreadPriority(&prio0, CUR_THREAD_HANDLE);
    Result rc_mask = 0, rc_prio = 0;
    if (mode && (!strcmp(mode, "spread") || !strcmp(mode, "mapped"))) {
        int index = __atomic_fetch_add(&next_index, 1, __ATOMIC_RELAXED);
        s32 prio = 59;
        if (!strcmp(mode, "mapped")) {
            int p = ps4_prio < 256 ? 256 : ps4_prio > 767 ? 767 : ps4_prio;
            prio = 44 + (p - 256) * 15 / 511;
        }
        rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, index % 3, 0x7);
        rc_prio = svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
        svcSleepThread(-1); /* yield with core migration so the new ideal core takes effect */
    }
    s32 prio1 = -1;
    svcGetThreadPriority(&prio1, CUR_THREAD_HANDLE);
    nx_platform_trace("Sched: '%s' ps4=%d core %d->%d prio %d->%d mode=%s rc=0x%x/0x%x\n", name ? name : "?",
                      ps4_prio, core0, (int)svcGetCurrentProcessorNumber(), (int)prio0, (int)prio1,
                      mode ? mode : "off", (unsigned)rc_mask, (unsigned)rc_prio);
    extern void nx_hle_thread_begin(const char *name);
    nx_hle_thread_begin(name);
    nx_cpu_register(name);
#else
    (void)name; (void)ps4_prio;
#endif
}

/* CPU census: every guest thread and the GPU thread, with the CPU time Horizon counted for it.
 * nx_platform_cpu_report() prints the busiest ones over the last interval. */
enum { NX_CPU_SLOTS = 160 };
typedef struct {
    char name[28];
    Handle handle;
    u64 last_ticks;
    int alive;
} NxCpuSlot;
static NxCpuSlot g_cpu_slots[NX_CPU_SLOTS];
static Mutex g_cpu_lock;
static __thread int t_cpu_slot = -1;

static int nx_thread_ticks(Handle handle, u64 *ticks) {
    if (R_SUCCEEDED(svcGetInfo(ticks, InfoType_ThreadTickCount, handle, TickCountInfo_Total))) return 1;
    return R_SUCCEEDED(svcGetInfo(ticks, InfoType_ThreadTickCountDeprecated, handle, TickCountInfo_Total));
}

static void nx_cpu_register(const char *name) {
    mutexLock(&g_cpu_lock);
    for (int i = 0; i < NX_CPU_SLOTS; ++i) {
        if (g_cpu_slots[i].alive) continue;
        NxCpuSlot *s = &g_cpu_slots[i];
        snprintf(s->name, sizeof s->name, "%s", name ? name : "?");
        s->handle = threadGetCurHandle();
        s->last_ticks = 0;
        nx_thread_ticks(s->handle, &s->last_ticks);
        s->alive = 1;
        t_cpu_slot = i;
        break;
    }
    mutexUnlock(&g_cpu_lock);
}

void nx_platform_thread_exit(void) {
    if (t_cpu_slot < 0) return;
    mutexLock(&g_cpu_lock);
    g_cpu_slots[t_cpu_slot].alive = 0;
    mutexUnlock(&g_cpu_lock);
    t_cpu_slot = -1;
}

int nx_platform_cpu_report_str(char *buf, size_t size) {
    if (!buf || size == 0) return 0;
    buf[0] = '\0';
#ifdef __SWITCH__
    static u64 last_wall;
    const u64 now = armGetSystemTick();
    const u64 wall = last_wall ? now - last_wall : 0;
    last_wall = now;
    typedef struct { const char *name; u64 ticks; } Use;
    Use use[NX_CPU_SLOTS];
    int n = 0;
    u64 total = 0;
    mutexLock(&g_cpu_lock);
    for (int i = 0; i < NX_CPU_SLOTS; ++i) {
        NxCpuSlot *s = &g_cpu_slots[i];
        u64 ticks;
        if (!s->alive || !nx_thread_ticks(s->handle, &ticks)) continue;
        const u64 d = ticks - s->last_ticks;
        s->last_ticks = ticks;
        total += d;
        if (d) use[n++] = (Use){s->name, d};
    }
    mutexUnlock(&g_cpu_lock);
    if (!wall) return 0;
    /* Busiest first; the rest only count toward the total. */
    int len = snprintf(buf, size, "CPU: total %.0f%% of one core;", 100.0 * (double)total / (double)wall);
    for (int k = 0; k < 10 && k < n; ++k) {
        int best = k;
        for (int j = k + 1; j < n; ++j)
            if (use[j].ticks > use[best].ticks) best = j;
        const Use tmp = use[k]; use[k] = use[best]; use[best] = tmp;
        if (len > 0 && len < (int)size)
            len += snprintf(buf + len, size - (size_t)len, " %s %.0f%%", use[k].name,
                            100.0 * (double)use[k].ticks / (double)wall);
    }
    return len > 0;
#else
    return 0;
#endif
}

void nx_platform_cpu_report(void) {
    char line[512];
    if (nx_platform_cpu_report_str(line, sizeof line)) {
        nx_platform_trace("%s\n", line);
    }
}

void nx_platform_set_realtime_thread(void) {
#ifdef __SWITCH__
    svcSetThreadPriority(CUR_THREAD_HANDLE, 40);
#endif
}

/* Host threads (GPU command processor, ...) run at the libnx default (priority 59, cores 0-2),
 * equal to every guest thread. nx.ini NX_<key>=prio[:core] moves one; always logs where it runs. */
void nx_platform_host_thread_setup(const char *name, const char *key) {
#ifdef __SWITCH__
    char var[64];
    snprintf(var, sizeof var, "DCP_NX_%s", key);
    const char *value = getenv(var);
    Result rc_mask = 0, rc_prio = 0;
    if (value && *value) {
        int prio = atoi(value);
        const char *colon = strchr(value, ':');
        if (colon) {
            int core = atoi(colon + 1);
            rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
        }
        if (prio >= 0x1c && prio <= 0x3f) rc_prio = svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
        svcSleepThread(-1); /* yield with core migration so a new core takes effect */
    } else if (!strcmp(key, "GPU_THREAD")) {
        rc_mask = svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 0x7);
        rc_prio = svcSetThreadPriority(CUR_THREAD_HANDLE, 48);
        svcSleepThread(-1);
    }
    s32 prio1 = -1;
    svcGetThreadPriority(&prio1, CUR_THREAD_HANDLE);
    nx_platform_trace("Sched: host '%s' core %d prio %d %s=%s rc=0x%x/0x%x\n", name, (int)svcGetCurrentProcessorNumber(),
                      (int)prio1, var + 4, value ? value : "off", (unsigned)rc_mask, (unsigned)rc_prio);
    nx_cpu_register(name);
#else
    (void)name; (void)key;
#endif
}

int nx_platform_safe_mode(void) {
#ifdef __SWITCH__
    const char *value = getenv("DCP_NX_SAFE");
    if (value && !strcmp(value, "0")) return 0;
    return 1; /* a port that has never booted keeps its guest hooks off until asked */
#else
    return 0;
#endif
}

int nx_platform_handheld(void) {
#ifdef __SWITCH__
    return appletGetOperationMode() == AppletOperationMode_Handheld;
#else
    return 0;
#endif
}

#ifdef __SWITCH__
static pthread_mutex_t g_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_trace_cv = PTHREAD_COND_INITIALIZER;
static char g_trace_buf[16384];
static size_t g_trace_len = 0;
static int g_trace_async = 0;
static int g_trace_stop = 0;
static pthread_t g_trace_thread;

static void flush_bringup_locked(const char *extra, size_t extra_len) {
    if (g_trace_len == 0 && extra_len == 0) return;
    FILE *f = fopen("sdmc:/switch/dragons_crown_pro/logs/bringup.log", "a");
    if (f) {
        if (g_trace_len) fwrite(g_trace_buf, 1, g_trace_len, f);
        if (extra_len) fwrite(extra, 1, extra_len, f);
        fclose(f);
    }
    g_trace_len = 0;
}

static void *nx_trace_worker(void *arg) {
    (void)arg;
    svcSetThreadPriority(CUR_THREAD_HANDLE, 61);
    char local[16384];
    for (;;) {
        pthread_mutex_lock(&g_trace_lock);
        while (!g_trace_stop && g_trace_len == 0) {
            pthread_cond_wait(&g_trace_cv, &g_trace_lock);
        }
        const size_t n = g_trace_len;
        if (n) {
            memcpy(local, g_trace_buf, n);
            g_trace_len = 0;
        }
        const int stop = g_trace_stop;
        pthread_mutex_unlock(&g_trace_lock);
        if (n) {
            FILE *f = fopen("sdmc:/switch/dragons_crown_pro/logs/bringup.log", "a");
            if (f) {
                fwrite(local, 1, n, f);
                fclose(f);
            }
        }
        if (stop) break;
    }
    return NULL;
}

static void nx_trace_start_async(void) {
    pthread_mutex_lock(&g_trace_lock);
    if (!g_trace_async) {
        g_trace_stop = 0;
        if (pthread_create(&g_trace_thread, NULL, nx_trace_worker, NULL) == 0) {
            g_trace_async = 1;
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}

static void nx_trace_stop_async(void) {
    pthread_mutex_lock(&g_trace_lock);
    if (!g_trace_async) {
        flush_bringup_locked(NULL, 0);
        pthread_mutex_unlock(&g_trace_lock);
        return;
    }
    g_trace_stop = 1;
    pthread_cond_signal(&g_trace_cv);
    pthread_mutex_unlock(&g_trace_lock);
    pthread_join(g_trace_thread, NULL);
    pthread_mutex_lock(&g_trace_lock);
    g_trace_async = 0;
    flush_bringup_locked(NULL, 0);
    pthread_mutex_unlock(&g_trace_lock);
}

void nx_platform_trace(const char *format, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(line, sizeof line, format, ap);
    va_end(ap);
    char entry[1088];
    const int entry_len = snprintf(entry, sizeof entry, "%8llu %s",
                                   (unsigned long long)(armTicksToNs(armGetSystemTick()) / 1000000ULL),
                                   line);
    pthread_mutex_lock(&g_trace_lock);
    printf("%s", line);
    if (!g_trace_async) fflush(stdout);
    if (entry_len > 0) {
        const size_t n = (size_t)entry_len < sizeof(entry) ? (size_t)entry_len : sizeof(entry) - 1;
        if (g_trace_async && !g_trace_stop && g_trace_len + n <= sizeof(g_trace_buf)) {
            memcpy(g_trace_buf + g_trace_len, entry, n);
            g_trace_len += n;
            pthread_cond_signal(&g_trace_cv);
        } else {
            flush_bringup_locked(entry, n);
        }
    }
    pthread_mutex_unlock(&g_trace_lock);
}
#else
void nx_platform_trace(const char *format, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(line, sizeof line, format, ap);
    va_end(ap);
    printf("%s", line);
    fflush(stdout);
}
#endif

static void nx_apply_clocks(int boost, int log_trace) {
#ifdef __SWITCH__
    const int handheld = nx_platform_handheld();
    Result rc = clkrstInitialize();
    if (R_FAILED(rc)) {
        if (log_trace) nx_platform_trace("Switch: clkrstInitialize failed rc=0x%x\n", rc);
        return;
    }
    ClkrstSession s = {0};
    u32 cpu_hz = 0, gpu_hz = 0, emc_hz = 0;
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_CpuBus, 3))) {
        clkrstSetClockRate(&s, boost ? 1785000000 : 1020000000);
        if (log_trace) clkrstGetClockRate(&s, &cpu_hz);
        clkrstCloseSession(&s);
    }
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_GPU, 3))) {
        clkrstSetClockRate(&s, boost ? (handheld ? 460800000 : 768000000)
                                    : (handheld ? 307200000 : 768000000));
        if (log_trace) clkrstGetClockRate(&s, &gpu_hz);
        clkrstCloseSession(&s);
    }
    if (R_SUCCEEDED(clkrstOpenSession(&s, PcvModuleId_EMC, 3))) {
        clkrstSetClockRate(&s, boost ? 1600000000 : (handheld ? 1331200000 : 1600000000));
        if (log_trace) clkrstGetClockRate(&s, &emc_hz);
        clkrstCloseSession(&s);
    }
    clkrstExit();
    if (log_trace) {
        nx_platform_trace("Switch: clkrst cpu=%uMHz gpu=%uMHz emc=%uMHz (boost=%d)\n",
                          cpu_hz / 1000000, gpu_hz / 1000000, emc_hz / 1000000, boost ? 1 : 0);
    }
#else
    (void)boost; (void)log_trace;
#endif
}

void nx_platform_set_boost(int boost) {
    const int on = boost != 0;
    setenv("DCP_NX_BOOST", on ? "1" : "0", 1);
    nx_apply_clocks(on, 1);
}

void nx_platform_init(void) {
    if (g_nx_initialized) return;
    g_nx_initialized = 1;
#ifdef __SWITCH__
    ensure_dir("sdmc:/switch/dragons_crown_pro");
    ensure_dir("sdmc:/switch/dragons_crown_pro/game");
    ensure_dir("sdmc:/switch/dragons_crown_pro/save");
    ensure_dir("sdmc:/switch/dragons_crown_pro/logs");
    ensure_dir("sdmc:/switch/dragons_crown_pro/user");
    ensure_dir("sdmc:/switch/dragons_crown_pro/cache");
    setenv("MESA_SHADER_CACHE_DIR", "sdmc:/switch/dragons_crown_pro/cache", 0);
    setenv("MESA_SHADER_CACHE_DISABLE", "false", 0);
    {
        time_t now = time(NULL);
        struct tm *tmv = localtime(&now);
        char stamp[32] = "?";
        if (tmv) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tmv);
        uint64_t htot = 0, hfree = 0;
        nx_platform_heap_info(&htot, &hfree);
        nx_platform_trace("--- bringup %s | applet=%d | RAM=%llu MiB | heap=%llu/%llu MiB free ---\n",
                          stamp, nx_platform_is_applet_mode(),
                          (unsigned long long)(nx_platform_total_ram_bytes() >> 20),
                          (unsigned long long)(hfree >> 20),
                          (unsigned long long)(htot >> 20));
    }
    socketInitializeDefault();
    g_nxlink_sock = nxlinkStdio();
    nx_platform_trace("Switch: nxlink fd=%d\n", g_nxlink_sock);
    appletLockExit();
    atexit(nx_platform_exit);
    load_ini();
    {
        const char *boost = getenv("DCP_NX_BOOST");
        if (!boost || strcmp(boost, "0") != 0) nx_apply_clocks(1, 1);
    }
    nx_large_arena_init();
    nx_trace_start_async();
    if (nx_platform_is_applet_mode())
        nx_platform_trace("Switch: APPLET MODE: limited memory; launch in Title Takeover\n");
    nx_platform_trace("Switch: init done\n");
#endif
}

/* Stock clocks (what the boost in nx_platform_init replaced): they outlive the process otherwise. */
static void nx_restore_clocks(void) {
    nx_apply_clocks(0, 0);
}

/* Graceful Switch process exit from any thread (HOME -> Close / SDL_QUIT).
 * Unlocking appletLockExit and driving libnx's _appletExitProcess path (via __nx_applet_exit_mode=1)
 * calls ISelfController::Exit() (Application) or ExitProcessAndReturn() (Applet) and sleeps while
 * Horizon AM terminates the process cleanly; raw svcExitProcess() triggers a Switch error dialog. */
void nx_platform_quit(void) {
#ifdef __SWITCH__
    extern u32 __nx_applet_exit_mode;
    nx_platform_trace("Switch: quitting (unlock + appletExit)\n");
    nx_restore_clocks();
    nx_platform_exit();
    __nx_applet_exit_mode = 1;
    LoaderReturnFn prev = envGetExitFuncPtr();
    appletExit();
    LoaderReturnFn next = envGetExitFuncPtr();
    if (next && next != prev) {
        next(0);
    }
    svcExitProcess();
#endif
}

void userAppExit(void) {
    nx_platform_exit();
}

void nx_platform_exit(void) {
    if (!g_nx_initialized) return;
    nx_platform_trace("Switch: nx_platform_exit called\n");
    g_nx_initialized = 0;
#ifdef __SWITCH__
    nx_trace_stop_async();
    if (g_nxlink_sock >= 0) {
        close(g_nxlink_sock);
        g_nxlink_sock = -1;
    }
    appletUnlockExit();
    socketExit();
#endif
}

int nx_platform_is_applet_mode(void) {
#ifdef __SWITCH__
    AppletType type = appletGetAppletType();
    return type != AppletType_Application && type != AppletType_SystemApplication;
#else
    return 0;
#endif
}

uint64_t nx_platform_total_ram_bytes(void) {
#ifdef __SWITCH__
    u64 size = 0;
    if (R_SUCCEEDED(svcGetInfo(&size, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) {
        return (uint64_t)size;
    }
#endif
    return 0;
}

void nx_platform_heap_info(uint64_t *heap_total, uint64_t *heap_free) {
#ifdef __SWITCH__
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    uint64_t total = (fake_heap_end > fake_heap_start)
                         ? (uint64_t)(uintptr_t)(fake_heap_end - fake_heap_start)
                         : 0;
    char *cur_brk = (char *)sbrk(0);
    uint64_t unmapped = (cur_brk && fake_heap_end > cur_brk)
                            ? (uint64_t)(uintptr_t)(fake_heap_end - cur_brk)
                            : 0;
    struct mallinfo mi = mallinfo();
    uint64_t free_bytes = unmapped + (uint64_t)(uint32_t)mi.fordblks + nx_large_arena_free_bytes();
    if (heap_total) *heap_total = total;
    if (heap_free) *heap_free = free_bytes;
#else
    if (heap_total) *heap_total = 0;
    if (heap_free) *heap_free = 0;
#endif
}

#if defined(__SWITCH__) && !defined(DCP_MESA_SWITCH)
int __real_nouveau_bo_new(void *dev, uint32_t flags, uint32_t align, uint64_t size, void *cfg, void **pbo);
int __wrap_nouveau_bo_new(void *dev, uint32_t flags, uint32_t align, uint64_t size, void *cfg, void **pbo) {
    static uint64_t total_ok = 0, calls_ok = 0, fails = 0;
    int rc = __real_nouveau_bo_new(dev, flags, align, size, cfg, pbo);
    if (rc == 0) {
        total_ok += size;
        ++calls_ok;
    } else if (++fails <= 24) {
        extern char *fake_heap_end;
        char *cur_brk = (char *)sbrk(0);
        uint64_t unmapped = (cur_brk && fake_heap_end > cur_brk)
                                ? (uint64_t)(uintptr_t)(fake_heap_end - cur_brk)
                                : 0;
        struct mallinfo mi = mallinfo();
        uint64_t au = 0, af = 0, abig = 0;
        int ana = 0, anf = 0;
        nx_large_arena_stats(&au, &af, &abig, &ana, &anf);
        nx_platform_trace("NOUVEAU_BO_FAIL[#%llu]: rc=%d(0x%x) sz=%lluKiB al=0x%x fl=0x%x ok=%llu(%lluMiB) unmap=%lluMiB ford=%uMiB arena=%llu/%lluMiB(big=%lluMiB n=%d/%d)\n",
                          (unsigned long long)fails, rc, (unsigned)(-rc),
                          (unsigned long long)(size >> 10), align, flags,
                          (unsigned long long)calls_ok, (unsigned long long)(total_ok >> 20),
                          (unsigned long long)(unmapped >> 20),
                          (unsigned)((uint32_t)mi.fordblks >> 20),
                          (unsigned long long)(au >> 20), (unsigned long long)((au + af) >> 20),
                          (unsigned long long)(abig >> 20), ana, anf);
    }
    return rc;
}
#endif

int nx_platform_has_nxlink(void) {
    return g_nxlink_sock >= 0;
}

void nx_platform_translate_path(const char *ps4_path, char *out, size_t out_size) {
    if (!out || !out_size) return;
    if (!ps4_path || !*ps4_path) {
        out[0] = '\0';
        return;
    }
    if (strncmp(ps4_path, "sdmc:/", 6) == 0 || strncmp(ps4_path, "romfs:/", 7) == 0) {
        snprintf(out, out_size, "%s", ps4_path);
        return;
    }
    if (strncmp(ps4_path, "/app0/", 6) == 0) {
        snprintf(out, out_size, "sdmc:/switch/dragons_crown_pro/game/%s", ps4_path + 6);
    } else if (strcmp(ps4_path, "/app0") == 0) {
        snprintf(out, out_size, "sdmc:/switch/dragons_crown_pro/game");
    } else if (strncmp(ps4_path, "/savedata0/", 11) == 0) {
        snprintf(out, out_size, "sdmc:/switch/dragons_crown_pro/save/%s", ps4_path + 11);
    } else if (strcmp(ps4_path, "/savedata0") == 0) {
        snprintf(out, out_size, "sdmc:/switch/dragons_crown_pro/save");
    } else if (strncmp(ps4_path, "/user/", 6) == 0) {
        snprintf(out, out_size, "sdmc:/switch/dragons_crown_pro/user/%s", ps4_path + 6);
    } else {
        snprintf(out, out_size, "%s", ps4_path);
    }
}
