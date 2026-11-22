#pragma once
#include <stdint.h>
#ifdef __aarch64__
static inline void _mm_pause(void) { __asm__ volatile("yield"); }
#ifndef __builtin_ia32_pause
#define __builtin_ia32_pause() _mm_pause()
#endif
static inline uint64_t __rdtsc(void) {
    uint64_t v = 0;
#ifdef __SWITCH__
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
#else
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
#endif
    return v;
}
#endif
