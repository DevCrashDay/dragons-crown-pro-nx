/* Box64 on Switch: no dynamic loader. dlsym/dlopen always fail. */
#pragma once
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT ((void*)-1)
typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
static inline void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline int dlclose(void *h) { (void)h; return 0; }
static inline char *dlerror(void) { return (char *)"no dynamic loader on Switch"; }
static inline int dladdr(const void *a, Dl_info *i) { (void)a; (void)i; return 0; }
