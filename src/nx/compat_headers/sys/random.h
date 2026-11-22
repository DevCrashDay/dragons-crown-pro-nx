#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __SWITCH__
#include <switch/services/csrng.h>
#endif

static inline ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
#ifdef __SWITCH__
    if (R_SUCCEEDED(csrngGetRandomBytes(buf, buflen))) return (ssize_t)buflen;
#endif
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < buflen; ++i) p[i] = (uint8_t)(rand() & 0xff);
    return (ssize_t)buflen;
}
