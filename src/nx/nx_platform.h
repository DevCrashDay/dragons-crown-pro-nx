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
int nx_platform_handheld(void);
/* Applies boosted (1) or stock (0) CPU/GPU/EMC clocks immediately via clkrst and updates DCP_NX_BOOST. */
void nx_platform_set_boost(int boost);
/* Updates or appends KEY=VALUE in sdmc:/switch/dragons_crown_pro/nx.ini (deletes KEY if empty) and applies env. */
int nx_platform_save_ini(const char *key, const char *value);

/* Called on each guest thread (and the main thread) before guest code: applies NX_SCHED and logs
   the thread's core and priority. */
void nx_platform_thread_setup(const char *name, int ps4_prio);

/* Host thread placement from nx.ini NX_<key>=prio[:core] (e.g. NX_GPU_THREAD=44:2); logs the result. */
void nx_platform_host_thread_setup(const char *name, const char *key);
void nx_platform_set_realtime_thread(void);

/* CPU census of the threads set up above: a thread leaves it with nx_platform_thread_exit();
   nx_platform_cpu_report() logs the busiest ones since the previous report. */
void nx_platform_thread_exit(void);
void nx_platform_cpu_report(void);
int nx_platform_cpu_report_str(char *buf, size_t size);

/* Ends the process from any thread (window closed / HOME close): restores stock clocks first. */
void nx_platform_quit(void);

/* Appends one line to sdmc:/switch/dragons_crown_pro/logs/bringup.log and to stdout.
   Works before the runtime log exists, so an early exit still leaves a reason on the SD. */
void nx_platform_trace(const char *format, ...);

#ifdef __cplusplus
}
#endif
