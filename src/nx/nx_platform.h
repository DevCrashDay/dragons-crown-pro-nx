#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nx_platform_init(void);
void nx_platform_exit(void);
void nx_platform_translate_path(const char *ps4_path, char *out, size_t out_size);
int nx_platform_is_applet_mode(void);
uint64_t nx_platform_total_ram_bytes(void);
void nx_platform_heap_info(uint64_t *heap_total, uint64_t *heap_free);
void nx_large_arena_init(void);
uint64_t nx_large_arena_free_bytes(void);
void nx_large_arena_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *largest_free, int *nalloc, int *nfree);
int nx_platform_has_nxlink(void);

/* nx.ini: DCP_* environment variables for a build that has no shell (see HANDOFF_NX.md).
   Safe mode disables the port's guest-hook features and is the default on Switch. */
int nx_platform_safe_mode(void);

/* Called on each guest thread (and the main thread) before guest code: applies NX_SCHED and logs
   the thread's core and priority. */
void nx_platform_thread_setup(const char *name, int ps4_prio);

/* Appends one line to sdmc:/switch/dragons_crown_pro/logs/bringup.log and to stdout.
   Works before the runtime log exists, so an early exit still leaves a reason on the SD. */
void nx_platform_trace(const char *format, ...);

#ifdef __cplusplus
}
#endif
