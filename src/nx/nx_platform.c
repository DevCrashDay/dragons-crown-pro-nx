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
        char name[160];
        snprintf(name, sizeof name, "DCP_%s", key);
        setenv(name, value, 1);
        ++count;
    }
    fclose(f);
    if (count) printf("Switch: %u settings from nx.ini\n", count);
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

#include <pthread.h>
static pthread_mutex_t g_trace_lock = PTHREAD_MUTEX_INITIALIZER;

void nx_platform_trace(const char *format, ...) {
    char line[600];
    va_list ap;
    va_start(ap, format);
    vsnprintf(line, sizeof line, format, ap);
    va_end(ap);
    pthread_mutex_lock(&g_trace_lock);
    printf("%s", line);
    fflush(stdout);
#ifdef __SWITCH__
    FILE *f = fopen("sdmc:/switch/dragons_crown_pro/logs/bringup.log", "a");
    if (f) {
        fputs(line, f);
        fclose(f);
    }
#endif
    pthread_mutex_unlock(&g_trace_lock);
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
    setenv("MESA_GLSL_CACHE_DIR", "sdmc:/switch/dragons_crown_pro/cache", 0);
    setenv("MESA_GLSL_CACHE_DISABLE", "false", 0);
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
    if (nx_platform_is_applet_mode())
        nx_platform_trace("Switch: APPLET MODE: limited memory; launch in Title Takeover\n");
    nx_platform_trace("Switch: init done\n");
#endif
}

void nx_platform_exit(void) {
    if (!g_nx_initialized) return;
    nx_platform_trace("Switch: nx_platform_exit called\n");
    g_nx_initialized = 0;
#ifdef __SWITCH__
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
    uint64_t free_bytes = unmapped + (uint64_t)(uint32_t)mi.fordblks;
    if (heap_total) *heap_total = total;
    if (heap_free) *heap_free = free_bytes;
#else
    if (heap_total) *heap_total = 0;
    if (heap_free) *heap_free = 0;
#endif
}

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
