/* Box64 on Switch: Linux CPU affinity API. Core pinning is left to libnx thread creation. */
#pragma once
#include_next <sched.h>
#include <string.h>
#include <sys/types.h>
#ifndef CPU_SETSIZE
#define CPU_SETSIZE 64
typedef struct { unsigned long long bits; } cpu_set_t;
#define CPU_ZERO(s) memset((s), 0, sizeof(cpu_set_t))
#define CPU_SET(c, s) ((s)->bits |= 1ull << (c))
#define CPU_ISSET(c, s) (((s)->bits >> (c)) & 1)
#define CPU_COUNT(s) __builtin_popcountll((s)->bits)
static inline int sched_setaffinity(pid_t p, size_t n, const cpu_set_t *s) { (void)p; (void)n; (void)s; return 0; }
static inline int sched_getaffinity(pid_t p, size_t n, cpu_set_t *s) { (void)p; (void)n; s->bits = 0x7; return 0; }
#endif
