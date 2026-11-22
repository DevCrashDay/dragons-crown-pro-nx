#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Runs a minimal SDL2 + EGL/OpenGL ES 3.0 test when a flag file or DCP_GPU_PROBE is set:
 * creates the window, prints the GL strings, compiles one program and presents 60 frames.
 * Returns -1 when the probe was not requested, 0 on success, 1 on failure. */
int nx_gpu_probe_run(void);

#ifdef __cplusplus
}
#endif
