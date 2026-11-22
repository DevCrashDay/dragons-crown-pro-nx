#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Runs a handful of hand-encoded x86-64 routines through Box64 when x64_selftest.flag or
 * X64_SELFTEST=1 is set: arithmetic, fs: TLS, a guest->host bridge, and host->guest nesting.
 * Returns -1 when not requested, 0 when every check passed, 1 otherwise. */
int nx_x64_selftest_run(void);

#ifdef __cplusplus
}
#endif
