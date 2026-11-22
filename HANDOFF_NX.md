# Dragon's Crown Pro — Nintendo Switch (`dragons-crown-pro-nx`) — Status & Plan

Last updated: 2026-11-21

## 0. Where the project is right now

- **Ryujinx 1.3.3 & Real Switch Hardware Boot Status:**
  - `build-nx/dragons_crown_pro.nro` builds cleanly and boots without crashing on **both Ryujinx 1.3.3 and real Switch hardware** (`2304 MiB direct + 256 MiB flexible`, Box64 ARM64 Dynarec `48 MiB` `jitCreate` pool, Desktop OpenGL 4.3 Core `Mesa 20.1.0-rc3`, `switch` stereo `AUDIO_S16LSB` audio, 4 SDL Switch Controllers, CRI FS `DRACRO.cpk` [split] + `DRACRO_US.cpk`, `RenderDLThread[0..11]`).
  - **Current Blocker (Black Screen, 1 draw/frame loop):** Both Ryujinx and real Switch run a steady 60fps loop (`frame 540` in Ryujinx, `frame 600` on Switch) submitting `1` fullscreen-clear draw per frame (`vs 0x4c12efc8, ps 0xdd6a0825, 4 indices`, `draws=515/515`, `glErr=0x0`, `center=(0,0,0) max=0`).
  - **Key Diagnostic Findings:**
    1. `reads` stops at `72` (`38901 KiB` with `dynarec=1`, `47` / `29840 KiB` with `DYNAREC=0`) before frame 26; `Font: system font set` and `SAVEDATA00` (which appear immediately after the 4 `guest` threads in `DragonsCrownProPC/logs/dragons_crown_pro_001.log`) are never reached.
    2. `DYNAREC=0` (interpreter) exhibits the exact same behavior (`frame 300`, `draws=280/280`, `max=0`), ruling out a Box64 dynarec-specific bug.
    3. Frame-100 HLE trace (`F100`) shows the main/render loop polling `sceGnmSubmitAndFlipCommandBuffers`, `sceVideoOutWaitVblank`, `sceGnmDispatchDirect` + `sceGnmDrawIndex`, `scePadReadState(1..4)`, `sceUserServiceGetEvent`, `sceNpCheckCallback`, and `sceKernelPollEventFlag`/`sceKernelSetEventFlag` (`bit 0x2`) + `scePthreadCondWait`/`Broadcast`.
- **Fixes landed in this session:**
  - `gpu/shim/imgui_impl_gles3.{h,cpp}`, `dcp_overlay.cpp`: Parameterized `InitImGuiGles3(glsl_version)` (`"#version 330 core"`) using `SDL_GL_GetProcAddress`, fixing `imgl3w` NULL crash.
  - `gpu/CMakeLists.txt`, `gpu/shim/nx_vk_stubs.cpp`: Enabled real `image_info.cpp`, `host_compatibility.cpp`, `liverpool_to_vk.cpp` (previously stubbed as `Undefined at 0`, which broke `BindTargets` & `FindDisplayBuffer`).
  - `gpu/shadps4/common/rdtsc.h`, `third_party/box64/.../arm64_emitter.h`, `src/nx/compat_headers/x86intrin.h`: Switched `CNTVCT_EL0` → `CNTPCT_EL0` on Switch (Horizon traps EL0 `CNTVCT_EL0`, which caused `0x4A8` crash at `sceKernelReadTsc()` on real hardware).
  - `src/runtime_audio.c`: Downmix 8ch→stereo and convert float→`AUDIO_S16LSB` on Switch (fixes `audio` sysmodule `0100000000000014` crash on real hardware).
  - `gpu/shim/window.cpp`: `SDL_WINDOW_FULLSCREEN` (`1920x1080`) on Switch, skipping desktop `FitWindowToArea` (`1849x1040`).
  - `gpu/shadps4/video_core/renderer_opengl/gl_presenter.cpp`, `gl_rasterizer.{h,cpp}`, `src/runtime_file.c`: Added `nx_platform_trace` + `fsync(1)` milestone logging (`draws`, `reads`/`KiB`, `center`/`max`, `glErr`) to `bringup.log`.
- **Next Steps (Ryujinx first):**
  1. Identify what the 4 `guest` threads (`runtime_thread.c`) and `EventFlag` `bit 0x2` spin (`sceKernelPollEventFlag`/`SetEventFlag`) are waiting on between `38901 KiB` read and `Font: system font set` (compare PC `Renderer: selected opengl` vs Ryujinx `NX_SAFE=1`; test `NX_SAFE=0` and trace `CRI FS` / `sceSaveData` / `sceNpTrophy` / `sceUserService` return values).
  2. Once Ryujinx advances past the black screen to Atlus/Vanillaware/title rendering (`max > 0`), re-deploy to Switch (`ftp://192.168.8.221:5000/switch/dragons_crown_pro/dragons_crown_pro.nro`).

## 1. Architecture summary

The PC port is not a C++ source port: it is a native x86-64 PS4 ELF loader (`src/loader.c`,
`DCPBOOT1` boot image) that maps `eboot.bin` + PRXs into memory and serves PS4 HLE calls from
`src/runtime_*.c`, with a shadPS4-derived GPU core (`gpu/`).

Switch port choices:
- CPU: Box64 (ARM64 interpreter and dynarec; currently interpreter-only) with `SysV -> AAPCS64`
  host thunks.
- GPU: `renderer_opengl` with the existing `Target::Gles300` shader path, via SDL2 + EGL and
  the Switch Mesa/nouveau driver (GLES only; no desktop GL, no Vulkan).
- Storage: PS4 paths mapped to `sdmc:/switch/dragons_crown_pro/...`, with split-file support for
  FAT32.
- SDL: SDL3 API shim over the devkitPro SDL2 port.

## 2. Current build state

Build command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_nx.ps1
```

What compiles and links:
- `dcp_keys`, `dcp_startup_phase`, `dcp_ui_text`
- `Zycore`, `Zydis`, `fmt`, `xxhash`, `miniz`
- `libdcpgpu.a` (OpenGL renderer, GLSL shader backend, GnmDriver/VideoOut, ImGui)
- Box64 interpreter + all four dynarec passes + arm64 asm (`libbox64nx.a`)
- Every runtime file and both new test modes

The `.nro` is produced at `build-nx/dragons_crown_pro.nro`. Dynarec is compiled but disabled at
runtime (`SET_BOX64ENV(dynarec, 0)`) until the W^X RW/RX work lands.

## 3. Verified behavior

### 3.1 Box64 self-test (Ryujinx 1.3.3, `X64_SELFTEST=1`)

```
--- bringup 2026-11-21 19:34:23 | applet=0 | RAM=3285 MiB ---
Switch: nxlink fd=-1
Switch: init done
main: start argc=0
x64 selftest: starting
x64 selftest: arithmetic ok (got 123461, want 123461)
x64 selftest: fs base ok (got 0x1122334455667788)
x64 selftest: bridge call ok (got 49, want 49)
x64 selftest: nested guest call ok (got 99, want 99)
x64 selftest: ok (0 failures)
```

### 3.2 Loader startup (Ryujinx, default pool)

```
Paths: boot=sdmc:/switch/dragons_crown_pro/game/boot.bin app0=... user=... logs=...
Log: not started
Boot: DCPBOOT1 ok, image 18841696 bytes, entry 0x54c690
Runtime: start flags=1
Runtime: stack canary ready
Image: 17 MiB loaded, 749 imports, 3 modules
Memory: preparing the guest pool
Memory: reserving 2752 MiB of 3285 MiB process RAM
Memory: allocation FAILED: 2752 MiB of 3285 MiB process RAM
FAIL: guest memory unavailable before starting GPU or game threads
```

This is the current stopping point in Ryujinx with the default pool.

### 3.3 Not yet verified

- Real hardware with the fixed `.nro` (paths + canary).
- GPU probe: SDL + EGL + GLES context, extension list, shader compile, swap.
- Import audit (`CPU_ONLY=1 CHECK_IMPORTS=1` with a small pool).
- Module initializers and the first guest entry.
- End-to-end split-CPK reads by the game.
- Audio, pads, touch, save data.

## 4. Switch runtime model (implemented)

- **GPU is OpenGL ES 3.0, not desktop GL/Vulkan.** `DCP_SWITCH_OPENGL_ONLY` forces the OpenGL
  backend, `RunsOnGles()` is true, the window asks for the ES profile, the overlay uses the ES3
  ImGui backend, and per-target blending falls back when `GL_EXT_draw_buffers_indexed` is absent.
  Presentation and gameplay shaders use the port's existing `Target::Gles300` path.
- **Guest virtual addresses are host pointers.** Horizon cannot map host pages at PS4 VAs, so
  `runtime_memory.c` `place()` returns `backing_base + phys` for direct/flexible mappings.
  Reservations own a real block that a commit reuses in place; `owned_data` makes
  `runtime_memory_write_backing` write through the guest address. This replaced the previous
  broken model that handed out unmapped PS4-range addresses.
- **Page protection is a no-op.** `GuestWriteWatch::TryArm` returns `nullopt` on Switch, so the
  GL texture cache compares content every frame instead of relying on faults.
- **Fibers are converted.** Each fiber owns a host stack, a separate guest stack and its own
  `NxX64FiberEmus` array; `Run`/`Switch`/`ReturnToThread` activate the right emulator set, so a
  suspended fiber never shares a Box64 emulator slot with the thread.
- **Safe mode is the default.** `NX_SAFE=1` skips all guest-hook features (cheats, boss battle,
  appearance, comms, online, voice, skip-intro, gallery, resolution/ultrawide/text patches) and
  clears the GPU feature bits. `NX_SAFE=0` enables them.
- **`nx.ini`**: each `KEY=VALUE` line becomes `DCP_KEY`, so the PC environment knobs work on the
  console. Keys currently used: `NX_SAFE`, `NX_DMEM_MB`, `NX_FMEM_MB`, `AUDIO`, `PAD_MAP`,
  `GPU_PROBE`, `X64_SELFTEST`, `CPU_ONLY`, `CHECK_IMPORTS`.
- **Split files**: a missing `NAME` opens `NAME.000`, `NAME.001`, ... as one read-only file
  (open/stat/access/read/pread/lseek/fstat/close, 64-bit offsets). The base file wins if present.
- **Bring-up log**: `sdmc:/switch/dragons_crown_pro/logs/bringup.log` gets a line at every early
  stage (applet mode, RAM, paths, boot header, image, memory pool, GPU, modules, entry) and the
  reason for every `fail()`. This works without nxlink and is the primary hardware diagnostic.
- **CPU checks disabled**: `cpu_usable()` passes and the LZCNT rewrite is off; Box64 implements
  the ISA. The stack canary uses libnx `randomGet()` on Switch.
- Minor fixes: SDL3 shim success-value wrappers (`SDL_InitSubSystem`, `SDL_GL_MakeCurrent`,
  `SDL_GL_SetSwapInterval`, `SDL_GetWindowBordersSize`), serialized `pread`/`pwrite`, and an
  `nftw()` implementation for save-directory deletion.

## 5. Test harness and SD layout

Expected SD layout:

```
/switch/
  dragons_crown_pro.nro
  dragons_crown_pro/
    nx.ini
    game/  boot.bin content.bin image.json eboot.bin
           DRACRO.CPK.000 DRACRO.CPK.001 DRACRO_US.CPK
           sce_module/ sce_sys/
    user/
    logs/
```

Tooling:
- `tools/build_nx.ps1` — build the `.nro`.
- `tools/prepare_nx.ps1` — copy/flatten the PC game folders, write `.nro` and `nx.ini`;
  `-SplitCpk` runs the splitter after copying.
- `tools/split_cpk.ps1` — split files larger than the part size into `.000`, `.001`, ...
  (default 3900 MiB, removes the source unless `-KeepSource`).

Startup tests (both exit after printing their result):
- `GPU_PROBE=1` (or `gpu_probe.flag`): window, EGL/GLES context, GL strings/extensions, one
  ES 3.0 program, 60 frames.
- `X64_SELFTEST=1` (or `x64_selftest.flag`): arithmetic, `fs:` TLS, bridge call, nested call.
- `CPU_ONLY=1`: skip the GPU (loader/Box64 work).
- `CHECK_IMPORTS=1`: list imports with no HLE implementation, then exit. On emulators combine
  with `CPU_ONLY=1`.

Ryujinx setup used for software tests:
- NRO and game files under `%APPDATA%\Ryujinx\sdcard\switch\dragons_crown_pro\`.
- `nx.ini` in that folder; the `.nro` is targeted directly on the command line.
- `Log: not started` is expected until `pipe2` is implemented; use `logs/bringup.log`.

Hardware: launch in **Title Takeover** (hold R on a game icon in hbmenu), not from the album
applet. The game allocates about 2.09 GiB of direct memory before its first frame.

## 6. Files added/changed (this port)

New:
- `src/nx/` glue: platform/paths, Box64 OS layer, SysV thunk, SDL3 shim, compat headers,
  POSIX/network stubs, GPU probe, x64 self-test.
- `tools/build_nx.ps1`, `tools/prepare_nx.ps1`, `tools/split_cpk.ps1`
- `patches/box64-switch.patch`
- `gpu/shim/nx_vk_stubs.cpp`
- `packaging/nx/README.txt`

Modified (Switch-specific paths):
- `CMakeLists.txt`, `gpu/CMakeLists.txt`
- `src/loader.c`, `src/runtime*.c/.cpp`, `src/guest_hook.c`, `src/isa_emulate.c`
- `launcher/paths.cpp` (Switch executable directory)
- `gpu/shim/*`, `gpu/shadps4/video_core/renderer_opengl/*`,
  `gpu/shadps4/video_core/renderer_factory.cpp`, and related common/video files.

## 7. Plan (ordered)

### P0 — Retry on hardware (now)
1. Copy the current `.nro` to `sd:/switch/dragons_crown_pro.nro` (the `SD` mirror is ready).
2. Launch in Title Takeover, run once, then read `logs/bringup.log`.
3. Expected new sequence: paths -> `DCPBOOT1 ok` -> `Runtime: start` -> canary -> image ->
   memory pool -> GPU -> module/entry.
4. Record the last line. If it says `applet=1`, the launch was not Title Takeover.

### P1 — Fix the guest pool sizing
- The default is now 2304 MiB direct + 448 MiB flexible; Ryujinx failed to `memalign(2752 MiB)`
  inside a 3285 MiB process. Log `mallinfo()` free heap, and size the pool from the free heap,
  not total RAM.
- Consider `svcSetHeapSize()` early in `nx_platform_init` to claim the app's full memory before
  the pool, then verify with the bringup log.
- Keep `NX_DMEM_MB` / `NX_FMEM_MB` as overrides; for CPU-only tests use e.g. 512/128.
- Target: pass `Memory: pool ready` on hardware, then on Ryujinx with a small pool.

### P2 — Import audit
- Run `CPU_ONLY=1 CHECK_IMPORTS=1 NX_DMEM_MB=512 NX_FMEM_MB=128` (Ryujinx first, then hardware
  with the normal pool for GPU symbols).
- Implement or explicitly stub every missing import that the game reaches; keep the list in this
  file or a generated report.

### P3 — First guest execution
- With a small pool and `CPU_ONLY=1`, check that module initializers run under Box64 in Ryujinx
  and where the first failure is.
- On hardware, use the full pool and look for `Module N: init ...` / `returned 0`, then
  `Entry: entering guest`.
- Expect the first failures in HLE callbacks with more than four arguments or floating-point
  arguments; add a generic marshalling call and convert the call sites that are reached.

### P4 — GPU bring-up
- Run `GPU_PROBE=1` on hardware. Confirm SDL video driver, EGL/GLES context, GL version,
  `GL_EXT_texture_compression_s3tc` and the ES3 shader compile.
- Then boot with the real renderer: `s3tc`, per-target blending fallback, texture upload,
  presentation. If the context or caps fail, relax/replace the requirement or add the missing
  GLES path.
- Ryujinx cannot validate EGL/Mesa; it only validates the CPU/loader side.

### P5 — Dynarec (performance gate)
- Implement `jitCreate` RW/RX aliasing: keep Box64 pointers as RX, translate writes through the
  RW alias at the emission/chunk/jump-patch sites, flush D-cache/invalidate I-cache.
- Re-run the x64 self-test with dynarec, then the game.
- Interpreter is functional but too slow for a playable game.

### P6 — Fibers, callbacks, features
- Fibers are converted; validate at the first `sceFiber*` use.
- Convert the roughly 99 `GUEST_HOOK_ABI` guest function pointers used by features, or keep them
  behind `NX_SAFE=1` until converted.
- Add a host->guest call with 6 integer + 8 SSE arguments and `xmm0`/hidden-pointer returns.
- Re-enable features one by one, testing each.

### P7 — Runtime logging, storage, input, audio, saves
- Implement `pipe2` (or a file-only log path) so the normal runtime log is produced on Switch.
- Verify split-CPK reads end-to-end in the game; extend the host-side `CpkArchive` reader if the
  text/appearance/Japanese-voice scans are wanted on FAT32.
- Verify Joy-Con/Pro Controller mapping, touch, rumble, 48 kHz audio, and save-data mount/write.

### P8 — Memory and performance tuning
- Profile RAM in Title Takeover (~3.2 GB); lower the direct/flexible pools or Box64 caches as
  needed. Tune the GL texture/stream budgets.
- Add an in-game Switch menu for resolution scale, FPS cap and clocks after the game is stable.

## 8. Known issues / limitations

- `runtime_log_start()` fails on Switch because `pipe2()` is a stub; only `bringup.log` is
  written. Stdout via nxlink is still useful on hardware.
- The host-side CPK reader used for text/appearance/voice feature scans expects one whole
  `DRACRO.CPK`; those scans do not work on a split/FAT32 card. The game's own reads do.
- `getdents` lists `DRACRO.CPK.000/.001` rather than the virtual base name (unverified whether
  the game enumerates the folder; opening by name works).
- Host->guest calls currently marshal at most four integer arguments and RAX (no XMM args or
  returns); feature callbacks that exceed this will be wrong.
- Dynarec is off; interpreter performance is far below playable.
- `third_party/box64` shows bulk line-ending modifications around a small functional patch; keep
  `patches/box64-switch.patch` as the source of truth.
- The port work is uncommitted on `main` (`src/nx/`, tools, handoff are untracked). Commit on a
  branch before the next hardware session.

## 9. Reproduction commands

```powershell
# build
powershell -ExecutionPolicy Bypass -File tools\build_nx.ps1

# prepare a folder (split for FAT32)
powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 `
    -Destination C:\local\switch\dragons_crown_pro -SplitCpk

# split an existing folder only
powershell -ExecutionPolicy Bypass -File tools\split_cpk.ps1 -Path <...>\game

# nxlink on hardware
nxlink -s -a <PC-IP>
```

`nx.ini` example for the first hardware boot:

```ini
NX_SAFE=1
# NX_DMEM_MB=2304
# NX_FMEM_MB=448
# GPU_PROBE=1
# X64_SELFTEST=1
```

For a Ryujinx CPU/loader test:

```ini
NX_SAFE=1
CPU_ONLY=1
CHECK_IMPORTS=1
NX_DMEM_MB=512
NX_FMEM_MB=128
AUDIO=none
```
