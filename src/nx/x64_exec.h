#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NxX64Context NxX64Context;

/* Initialize the x86-64 -> AArch64 execution engine over the mapped guest memory [mem_base, mem_base + mem_size). */
NxX64Context *nx_x64_create(void *mem_base, size_t mem_size);
void nx_x64_destroy(NxX64Context *ctx);

/* Register an HLE host thunk at guest stub address `guest_stub` for PS4 import `name`. */
void nx_x64_register_import(NxX64Context *ctx, uint64_t guest_stub, const char *name, void *host_fn);

/* Guest-callable x86-64 address that calls AArch64 `host_fn` with the guest's SysV arguments
   (Box64 bridge: int3 'S' 'C' wrapper fn ret). Stable for the process lifetime. */
uint64_t nx_x64_bridge(void *host_fn);
uint64_t nx_x64_bridge_named(void *host_fn, const char *name);

/* Guest TCB for the calling host thread (the loader rewrites guest fs: to gs:). Used by
   nx_x64_call when its tls argument is 0. */
void nx_x64_set_tls_base(uint64_t base);

/* Fibers switch host stacks inside HLE calls, so their Box64 emulator state cannot live in the
   thread's depth-indexed array: a suspended fiber would share a slot with the thread. Each fiber
   gets its own array and activates it for as long as it runs (NULL selects the thread's own). */
typedef struct NxX64FiberEmus NxX64FiberEmus;
NxX64FiberEmus *nx_x64_fiber_emus_new(void);
void nx_x64_fiber_emus_free(NxX64FiberEmus *emus);
void nx_x64_fiber_emus_activate(NxX64FiberEmus *emus);

/* Execute guest x86-64 code starting at `entry_rip` with `rsp` and `gs_base`, returning guest RAX. */
uint64_t nx_x64_call(NxX64Context *ctx, uint64_t entry_rip, uint64_t rsp, uint64_t gs_base,
                     uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx);

/* Call a guest function pointer from host code (callbacks, destructors). rsp 0: run below the
   current guest frame when nested in a guest->host call, else on a per-thread fallback stack. */
#define NX_GUEST_CALL(fn, a, b, c, d)                                                          \
    nx_x64_call(NULL, (uint64_t)(uintptr_t)(fn), 0, 0, (uint64_t)(uintptr_t)(a),               \
                (uint64_t)(uintptr_t)(b), (uint64_t)(uintptr_t)(c), (uint64_t)(uintptr_t)(d))

#ifdef __cplusplus
}
#endif
