/* Dedicated page-aligned coalescing arena for large (>= 256 KiB, <= 128 MiB) allocations on Switch.
 *
 * Newlib dlmalloc on Horizon has HAVE_MMAP=0, so repeated 8-16 MiB Mesa/nouveau staging & texture
 * BO allocations interleave with small allocations and shred 300+ MiB of free heap into < 8 MiB
 * fragments (causing GL_OUT_OF_MEMORY on 4096x4096 BC7 atlases). Keeping large blocks in their own
 * coalescing pool prevents small allocations from pinning or splitting freed large spans. */
#ifdef __SWITCH__
#include "nx_platform.h"
#include <malloc.h>
#include <reent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define ARENA_MIN_BYTES  (512u * 1024u)
#define ARENA_MAX_BYTES  (128u * 1024u * 1024u)
#define ARENA_PAGE       4096u
#define ARENA_MAX_SPANS  2048

typedef struct {
    uint64_t off;
    uint64_t size;
} ArenaSpan;

static Mutex g_arena_lock;
static uint8_t *g_arena_base;
static uint64_t g_arena_size;
static uint64_t g_arena_used;
static ArenaSpan g_allocs[ARENA_MAX_SPANS];
static int g_nalloc;
static ArenaSpan g_frees[ARENA_MAX_SPANS + 1];
static int g_nfree;

void *__real__malloc_r(struct _reent *r, size_t size);
void *__real__calloc_r(struct _reent *r, size_t n, size_t size);
void *__real__memalign_r(struct _reent *r, size_t align, size_t size);
void *__real__realloc_r(struct _reent *r, void *ptr, size_t size);
void __real__free_r(struct _reent *r, void *ptr);
size_t __real__malloc_usable_size_r(struct _reent *r, void *ptr);

static inline uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

static inline int in_arena(const void *ptr) {
    if (!g_arena_base || !ptr) return 0;
    uintptr_t p = (uintptr_t)ptr, b = (uintptr_t)g_arena_base;
    return p >= b && p < b + (uintptr_t)g_arena_size;
}

static void free_insert_locked(uint64_t off, uint64_t size) {
    if (!size) return;
    int pos = 0;
    while (pos < g_nfree && g_frees[pos].off < off) ++pos;
    if (pos > 0 && g_frees[pos - 1].off + g_frees[pos - 1].size == off) {
        g_frees[pos - 1].size += size;
        if (pos < g_nfree && g_frees[pos - 1].off + g_frees[pos - 1].size == g_frees[pos].off) {
            g_frees[pos - 1].size += g_frees[pos].size;
            memmove(&g_frees[pos], &g_frees[pos + 1], (size_t)(g_nfree - pos - 1) * sizeof(ArenaSpan));
            --g_nfree;
        }
        return;
    }
    if (pos < g_nfree && off + size == g_frees[pos].off) {
        g_frees[pos].off = off;
        g_frees[pos].size += size;
        return;
    }
    if (g_nfree >= ARENA_MAX_SPANS + 1) return;
    memmove(&g_frees[pos + 1], &g_frees[pos], (size_t)(g_nfree - pos) * sizeof(ArenaSpan));
    g_frees[pos] = (ArenaSpan){off, size};
    ++g_nfree;
}

static int alloc_find_locked(uint64_t off) {
    int lo = 0, hi = g_nalloc - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (g_allocs[mid].off == off) return mid;
        if (g_allocs[mid].off < off) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void alloc_insert_locked(uint64_t off, uint64_t size) {
    int lo = 0, hi = g_nalloc;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (g_allocs[mid].off < off) lo = mid + 1;
        else hi = mid;
    }
    memmove(&g_allocs[lo + 1], &g_allocs[lo], (size_t)(g_nalloc - lo) * sizeof(ArenaSpan));
    g_allocs[lo] = (ArenaSpan){off, size};
    ++g_nalloc;
}

static void *arena_alloc(size_t size, size_t align) {
    if (!g_arena_base || size < ARENA_MIN_BYTES || size > ARENA_MAX_BYTES) return NULL;
    uint64_t req = align_up((uint64_t)size, ARENA_PAGE);
    uint64_t al = align < ARENA_PAGE ? ARENA_PAGE : align;
    if ((al & (al - 1)) != 0) return NULL; /* non-power-of-two align -> fallback */

    mutexLock(&g_arena_lock);
    if (g_nalloc >= ARENA_MAX_SPANS) {
        mutexUnlock(&g_arena_lock);
        return NULL;
    }
    int best = -1;
    uint64_t best_waste = UINT64_MAX;
    uint64_t best_start = 0;
    uint64_t base_addr = (uintptr_t)g_arena_base;
    for (int i = 0; i < g_nfree; ++i) {
        uint64_t f_start = g_frees[i].off;
        uint64_t f_end = f_start + g_frees[i].size;
        uint64_t start = align_up(base_addr + f_start, al) - base_addr;
        if (start < f_end && f_end - start >= req) {
            uint64_t waste = g_frees[i].size - req;
            if (waste < best_waste) {
                best_waste = waste;
                best = i;
                best_start = start;
                if (waste == 0) break;
            }
        }
    }
    if (best < 0) {
        mutexUnlock(&g_arena_lock);
        return NULL;
    }
    ArenaSpan f = g_frees[best];
    memmove(&g_frees[best], &g_frees[best + 1], (size_t)(g_nfree - best - 1) * sizeof(ArenaSpan));
    --g_nfree;
    uint64_t prefix = best_start - f.off;
    uint64_t suffix_off = best_start + req;
    uint64_t suffix = (f.off + f.size) - suffix_off;
    if (prefix) free_insert_locked(f.off, prefix);
    if (suffix) free_insert_locked(suffix_off, suffix);
    alloc_insert_locked(best_start, req);
    g_arena_used += req;
    mutexUnlock(&g_arena_lock);
    return g_arena_base + best_start;
}

static uint64_t arena_size_of(const void *ptr) {
    uint64_t off = (uint64_t)((uintptr_t)ptr - (uintptr_t)g_arena_base);
    mutexLock(&g_arena_lock);
    int idx = alloc_find_locked(off);
    uint64_t sz = idx >= 0 ? g_allocs[idx].size : 0;
    mutexUnlock(&g_arena_lock);
    return sz;
}

static void arena_free(void *ptr) {
    uint64_t off = (uint64_t)((uintptr_t)ptr - (uintptr_t)g_arena_base);
    mutexLock(&g_arena_lock);
    int idx = alloc_find_locked(off);
    if (idx >= 0) {
        uint64_t sz = g_allocs[idx].size;
        memmove(&g_allocs[idx], &g_allocs[idx + 1], (size_t)(g_nalloc - idx - 1) * sizeof(ArenaSpan));
        --g_nalloc;
        g_arena_used -= sz;
        free_insert_locked(off, sz);
    }
    mutexUnlock(&g_arena_lock);
}

void nx_large_arena_init(void) {
    if (g_arena_base) return;
    mutexInit(&g_arena_lock);
    uint64_t htot = 0, hfree = 0;
    nx_platform_heap_info(&htot, &hfree);
    uint64_t free_mb = hfree >> 20;
    const char *env = getenv("DCP_NX_LARGE_ARENA_MB");
    uint64_t mb = env ? strtoull(env, NULL, 10) : 800;
    if (mb == 0) return;
    if (free_mb < 1024) {
        mb = free_mb / 4;
    } else if (mb > free_mb / 3) {
        mb = free_mb / 3;
    }
    if (mb < 32) return;
    uint64_t bytes = mb * 1024ULL * 1024ULL;
    void *mem = __real__memalign_r(_REENT, ARENA_PAGE, (size_t)bytes);
    if (!mem) {
        nx_platform_trace("LargeArena: failed to reserve %llu MiB\n", (unsigned long long)mb);
        return;
    }
    g_arena_base = (uint8_t *)mem;
    g_arena_size = bytes;
    g_arena_used = 0;
    g_nalloc = 0;
    g_frees[0] = (ArenaSpan){0, bytes};
    g_nfree = 1;
    nx_platform_trace("LargeArena: reserved %llu MiB at %p (min=%uKiB)\n",
                      (unsigned long long)mb, mem, ARENA_MIN_BYTES >> 10);
}

uint64_t nx_large_arena_free_bytes(void) {
    if (!g_arena_base) return 0;
    mutexLock(&g_arena_lock);
    uint64_t f = g_arena_size - g_arena_used;
    mutexUnlock(&g_arena_lock);
    return f;
}

void nx_large_arena_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *largest_free, int *nalloc, int *nfree) {
    if (!g_arena_base) {
        if (used_bytes) *used_bytes = 0;
        if (free_bytes) *free_bytes = 0;
        if (largest_free) *largest_free = 0;
        if (nalloc) *nalloc = 0;
        if (nfree) *nfree = 0;
        return;
    }
    mutexLock(&g_arena_lock);
    uint64_t big = 0;
    for (int i = 0; i < g_nfree; ++i)
        if (g_frees[i].size > big) big = g_frees[i].size;
    if (used_bytes) *used_bytes = g_arena_used;
    if (free_bytes) *free_bytes = g_arena_size - g_arena_used;
    if (largest_free) *largest_free = big;
    if (nalloc) *nalloc = g_nalloc;
    if (nfree) *nfree = g_nfree;
    mutexUnlock(&g_arena_lock);
}

void *__wrap__malloc_r(struct _reent *r, size_t size) {
    if (size >= ARENA_MIN_BYTES && size <= ARENA_MAX_BYTES) {
        void *p = arena_alloc(size, ARENA_PAGE);
        if (p) return p;
    }
    return __real__malloc_r(r, size);
}

void *__wrap__calloc_r(struct _reent *r, size_t n, size_t size) {
    if (n && size && n <= SIZE_MAX / size) {
        size_t total = n * size;
        if (total >= ARENA_MIN_BYTES && total <= ARENA_MAX_BYTES) {
            void *p = arena_alloc(total, ARENA_PAGE);
            if (p) {
                memset(p, 0, total);
                return p;
            }
        }
    }
    return __real__calloc_r(r, n, size);
}

void *__wrap__memalign_r(struct _reent *r, size_t align, size_t size) {
    if (size >= ARENA_MIN_BYTES && size <= ARENA_MAX_BYTES) {
        void *p = arena_alloc(size, align);
        if (p) return p;
    }
    return __real__memalign_r(r, align, size);
}

void __wrap__free_r(struct _reent *r, void *ptr) {
    if (in_arena(ptr)) {
        arena_free(ptr);
        return;
    }
    __real__free_r(r, ptr);
}

size_t __wrap__malloc_usable_size_r(struct _reent *r, void *ptr) {
    if (in_arena(ptr)) {
        return (size_t)arena_size_of(ptr);
    }
    return __real__malloc_usable_size_r(r, ptr);
}

void *__wrap__realloc_r(struct _reent *r, void *ptr, size_t size) {
    if (!ptr) return __wrap__malloc_r(r, size);
    if (in_arena(ptr)) {
        if (size == 0) {
            arena_free(ptr);
            return NULL;
        }
        uint64_t old_sz = arena_size_of(ptr);
        uint64_t req = align_up((uint64_t)size, ARENA_PAGE);
        if (size >= ARENA_MIN_BYTES && size <= ARENA_MAX_BYTES && req <= old_sz) {
            if (req < old_sz) {
                uint64_t off = (uint64_t)((uintptr_t)ptr - (uintptr_t)g_arena_base);
                mutexLock(&g_arena_lock);
                int idx = alloc_find_locked(off);
                if (idx >= 0 && g_allocs[idx].size == old_sz) {
                    g_allocs[idx].size = req;
                    g_arena_used -= (old_sz - req);
                    free_insert_locked(off + req, old_sz - req);
                }
                mutexUnlock(&g_arena_lock);
            }
            return ptr;
        }
        void *dst = (size >= ARENA_MIN_BYTES && size <= ARENA_MAX_BYTES)
                        ? arena_alloc(size, ARENA_PAGE)
                        : NULL;
        if (!dst) dst = __real__malloc_r(r, size);
        if (!dst) return NULL;
        memcpy(dst, ptr, size < old_sz ? size : (size_t)old_sz);
        arena_free(ptr);
        return dst;
    }
    if (size >= ARENA_MIN_BYTES && size <= ARENA_MAX_BYTES) {
        size_t old_sz = __real__malloc_usable_size_r(r, ptr);
        void *dst = arena_alloc(size, ARENA_PAGE);
        if (dst) {
            memcpy(dst, ptr, size < old_sz ? size : old_sz);
            __real__free_r(r, ptr);
            return dst;
        }
    }
    return __real__realloc_r(r, ptr, size);
}
#endif
