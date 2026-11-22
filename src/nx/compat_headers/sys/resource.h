#pragma once
#include <sys/time.h>

#ifndef RUSAGE_SELF
#define RUSAGE_SELF 0
#define RUSAGE_THREAD 1
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_minflt;
    long ru_majflt;
    long ru_nvcsw;
    long ru_nivcsw;
};
static inline int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (usage) {
        __builtin_memset(usage, 0, sizeof(*usage));
    }
    return 0;
}
#endif
