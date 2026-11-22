/* Box64 bring-up test: a few hand-encoded x86-64 routines, no game image involved. */
#include "nx_x64_selftest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x64_exec.h"
#include "nx_platform.h"

typedef struct {
    uint8_t bytes[256];
    size_t used;
} Blob;

static void put(Blob *b, const uint8_t *data, size_t size) {
    if (b->used + size > sizeof b->bytes) return;
    memcpy(b->bytes + b->used, data, size);
    b->used += size;
}

static void mov_rax_imm64(Blob *b, uint64_t value) {
    const uint8_t op[2] = {0x48, 0xB8};
    put(b, op, sizeof op);
    put(b, (const uint8_t *)&value, sizeof value);
}

static void ret(Blob *b) {
    const uint8_t op = 0xC3;
    put(b, &op, 1);
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int requested(void) {
    const char *env = getenv("DCP_X64_SELFTEST");
    if (env && *env && strcmp(env, "0")) return 1;
    return file_exists("sdmc:/switch/dragons_crown_pro/x64_selftest.flag") ||
           file_exists("x64_selftest.flag");
}

static uint64_t host_add7(uint64_t value) { return value + 7; }

static uint64_t host_sum9(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
                          uint64_t e, uint64_t f, uint64_t g, uint64_t h, uint64_t i) {
    return a * 1 + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9;
}

/* The inner guest routine, entered from the bridge's host function (host -> guest nesting). */
static uint64_t inner_entry;

static uint64_t host_nested(void) {
    return NX_GUEST_CALL(inner_entry, 0, 0, 0, 0);
}

static uint8_t guest_stack[64 * 1024];

static uint64_t run(NxX64Context *ctx, const Blob *blob, uint64_t fs_base) {
    const uint64_t top = (uint64_t)(uintptr_t)(guest_stack + sizeof guest_stack - 32);
    return nx_x64_call(ctx, (uint64_t)(uintptr_t)blob->bytes, top, fs_base, 0, 0, 0, 0);
}

int nx_x64_selftest_run(void) {
    NxX64Context *ctx;
    Blob arith = {{0}, 0}, tls = {{0}, 0}, bridge = {{0}, 0}, nested = {{0}, 0}, inner = {{0}, 0};
    uint64_t tls_value = UINT64_C(0x1122334455667788);
    uint64_t tls_entry;
    int failures = 0;
    uint64_t got;

    if (!requested()) return -1;
    nx_platform_trace("x64 selftest: starting\n");
    fflush(stdout);
    ctx = nx_x64_create(NULL, 0);
    if (!ctx) {
        nx_platform_trace("x64 selftest: Box64 context creation failed\n");
        return 1;
    }

    /* mov rax, 123456; add rax, 5; ret */
    {
        const uint8_t add[4] = {0x48, 0x83, 0xC0, 0x05};
        mov_rax_imm64(&arith, 123456);
        put(&arith, add, sizeof add);
        ret(&arith);
    }
    got = run(ctx, &arith, 0);
    nx_platform_trace("x64 selftest: arithmetic %s (got %llu, want 123461)\n",
           got == 123461 ? "ok" : "FAIL", (unsigned long long)got);
    failures += got != 123461;

    /* mov rax, qword [fs:0]; ret */
    {
        const uint8_t fs_load[9] = {0x64, 0x48, 0x8B, 0x04, 0x25, 0, 0, 0, 0};
        put(&tls, fs_load, sizeof fs_load);
        ret(&tls);
    }
    tls_entry = (uint64_t)(uintptr_t)tls.bytes;
    got = nx_x64_call(ctx, tls_entry, (uint64_t)(uintptr_t)(guest_stack + sizeof guest_stack - 32),
                      (uint64_t)(uintptr_t)&tls_value, 0, 0, 0, 0);
    nx_platform_trace("x64 selftest: fs base %s (got 0x%llx)\n",
           got == tls_value ? "ok" : "FAIL", (unsigned long long)got);
    failures += got != tls_value;

    /* mov rax, <bridge>; mov edi, 42; call rax; ret */
    {
        const uint8_t call[7] = {0xBF, 42, 0, 0, 0, 0xFF, 0xD0};
        mov_rax_imm64(&bridge, nx_x64_bridge((void *)host_add7));
        put(&bridge, call, sizeof call);
        ret(&bridge);
    }
    got = run(ctx, &bridge, 0);
    nx_platform_trace("x64 selftest: bridge call %s (got %llu, want 49)\n",
           got == 49 ? "ok" : "FAIL", (unsigned long long)got);
    failures += got != 49;

    /* Guest calls the bridge, whose host function calls back into guest code. */
    {
        const uint8_t mov99[10] = {0x48, 0xB8, 99, 0, 0, 0, 0, 0, 0, 0};
        const uint8_t call[2] = {0xFF, 0xD0};
        put(&inner, mov99, sizeof mov99);
        ret(&inner);
        inner_entry = (uint64_t)(uintptr_t)inner.bytes;
        mov_rax_imm64(&nested, nx_x64_bridge((void *)host_nested));
        put(&nested, call, sizeof call);
        ret(&nested);
    }
    got = run(ctx, &nested, 0);
    nx_platform_trace("x64 selftest: nested guest call %s (got %llu, want 99)\n",
           got == 99 ? "ok" : "FAIL", (unsigned long long)got);
    failures += got != 99;

    /* 9-arg SysV -> AAPCS64 (x0..x7 + stack): push 9; push 8; push 7; mov rdi..r9=1..6; call rax; add rsp, 24; ret */
    {
        Blob args9 = {{0}, 0};
        const uint8_t code[] = {
            0x6A, 0x09,                         /* push 9 */
            0x6A, 0x08,                         /* push 8 */
            0x6A, 0x07,                         /* push 7 */
            0xBF, 0x01, 0x00, 0x00, 0x00,       /* mov edi, 1 */
            0xBE, 0x02, 0x00, 0x00, 0x00,       /* mov esi, 2 */
            0xBA, 0x03, 0x00, 0x00, 0x00,       /* mov edx, 3 */
            0xB9, 0x04, 0x00, 0x00, 0x00,       /* mov ecx, 4 */
            0x41, 0xB8, 0x05, 0x00, 0x00, 0x00, /* mov r8d, 5 */
            0x41, 0xB9, 0x06, 0x00, 0x00, 0x00, /* mov r9d, 6 */
            0xFF, 0xD0,                         /* call rax */
            0x48, 0x83, 0xC4, 0x18,             /* add rsp, 24 */
        };
        mov_rax_imm64(&args9, nx_x64_bridge((void *)host_sum9));
        put(&args9, code, sizeof code);
        ret(&args9);
        got = run(ctx, &args9, 0);
        nx_platform_trace("x64 selftest: 9-arg ABI %s (got %llu, want 285)\n",
               got == 285 ? "ok" : "FAIL", (unsigned long long)got);
        failures += got != 285;
    }

    nx_x64_destroy(ctx);
    nx_platform_trace("x64 selftest: %s (%d failure%s)\n", failures ? "FAILED" : "ok", failures,
           failures == 1 ? "" : "s");
    fflush(stdout);
    return failures ? 1 : 0;
}
