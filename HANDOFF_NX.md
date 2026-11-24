# Dragon's Crown Pro — Nintendo Switch (`dragons-crown-pro-nx`) — Status & Plan

Last updated: 2026-11-23. Branch: `switch-port`. This is the only handoff document
(the former `..\handoff.md` is merged here and archived in `..\_archive\2026-11-22\`).

## 0. Status

- **Goal:** full playable build on real Nintendo Switch hardware (Title Takeover, Mesa 26.2.4 `nouveau` OpenGL 4.3 Core / GLES 3.2).
- **Build:** `build-nx/dragons_crown_pro.nro` builds cleanly (`tools/build_nx.ps1`), deployed via FTP (`ftp://192.168.8.221:5000/switch/dragons_crown_pro/`).
- **2026-11-23: Real Switch hardware bringup — boots, renders, audio works, passes tutorial.**
  - **Hardware crashes fixed:**
    1. `third_party/box64/src/dynarec/arm64/updateflags_arm64.c` (`a881f64`): Data Abort (`0x4A8`) writing `block`/`NULL` through `next` (RX) instead of `rw_next` (RW).
    2. `src/nx/box64_os_nx.c` (`7b727da`): Data Abort (`0x4A8`) in `sysv_wrapper` reading 8 stack args past the mapped stack-top page on guest thread exit; guarded with `svcQueryMemory` when `(sp & 0xFFF) > 0xFC0`.
    3. `src/runtime_cheats.cpp`: disabled `runtime_cheats_hooks_wanted` on `__SWITCH__` (`PlayerTickHook` in `cheat_hooks.cpp` called x86_64 guest pointers directly from ARM64, causing Instruction Abort after character creation under `NX_SAFE=0`).
  - **Hardware blackscreen fixed:**
    - `gpu/shadps4/video_core/renderer_opengl/gl_texture_cache.cpp` (`FindDisplayBuffer`): on Switch, `CB_COLOR0_BASE` aligns render targets down to 64 KiB (`0x1ee6890000`) while `sceVideoOut` passes `0x1ee6894000` (`+0x4000`). Added a `< 0x10000` range-match fallback in `FindDisplayBuffer` for `gpu_written` `GL_TEXTURE_2D` targets with matching dimensions.
  - **Handheld 720p + top-left FPS overlay:**
    - `src/nx/nx_platform.c` / `src/loader.c`: added `nx_platform_handheld()` (`appletGetOperationMode() == AppletOperationMode_Handheld`), defaulting guest `render_resolution` to `720p` (`1280x720`) while keeping `WindowSDL` at `1920x1080` (`1280x720` + `SDL_WINDOW_FULLSCREEN` failed `eglMakeCurrent` with `EGL_BAD_SURFACE`).
    - `gpu/shim/dcp_settings.h`: defaulted `show_fps=true`, `overlay_corner=0` (top-left) on `__SWITCH__`; added `cntpct_el0` frame-time breakdown to `Presenter: show`.
  - **Why FPS was low on Switch hardware (4–5 FPS → 10–11 FPS, next fix built):**
    1. **Stock handheld clocks (1020 MHz CPU / 307 MHz GPU) + full texture hashing:** `dragons_crown_pro_064.log` showed `198.6 ms/frame` (`5.0 fps`) at ~237 draws/frame (`draw=147.7 ms`, `ref=17.4 ms`, `gl=5.8 ms`, `swap=0.7 ms`, guest CPU ~`50 ms`). Fixed with `clkrst` boost (`1785 MHz` CPU / `460 MHz` handheld GPU / `1600 MHz` EMC) in `src/nx/nx_platform.c` and sampled `HashGuestRange` in `TextureCache::Refresh` (`gl_texture_cache.cpp`), doubling FPS to **10.1–11.4 FPS** (`tot=92.9–99.4 ms`, `ref=0.3–0.7 ms` in `dragons_crown_pro_065.log`).
    2. **`glTexSubImage2D` stall in `StreamRing::UploadPending` (~50 ms/frame):** sub-profiling in `dragons_crown_pro_065.log` isolated **`49.2–53.3 ms/frame` (75% of `draw`)** to `cmt` (`CommitStreams()` → `glTexSubImage2D` into the `4096×4096` `GL_R32UI` `dcp_ring` texture on every draw, forcing Mesa `nouveau` to stall/stage). Switched `gles_shader_ring` on `__SWITCH__` to `RingStorage::Mapped` (`GL_TEXTURE_BUFFER` + `glMapBufferRange(GL_MAP_UNSYNCHRONIZED_BIT)` with `#version 320 es` `usamplerBuffer` in `gl_api.cpp`, `gl_rasterizer.cpp`, `emit_glsl.cpp`). Built and ready to upload/test.
    3. **Remaining ~25–30 ms/frame guest CPU + `stg`/`shd`:** Box64 currently defaults to `BOX64_DYNAREC_STRONGMEM=1` (ARM64 `dmb` barriers), and `stg`/`shd` take ~`8 + 8 ms` at ~200+ draws/frame.
- **Works (Ryujinx Canary 1.3.354):** paths, `DCPBOOT1`, 2176+32 MiB guest pool, 800 MiB large-block
  arena, Box64 dynarec (`BOX64_DYNAREC_STRONGMEM=1` default) and interpreter, GL 4.3 core on Mesa
  nouveau, direct `libnx audout` stereo audio, 3 module initializers, guest entry, 4 pads, split CPK
  (`DRACRO.CPK.000/.001`), runtime log (`logs/dragons_crown_pro_NNN.log`) and `logs/bringup.log`.
- **2026-11-22 late evening: character rendering, OOM, audio, and Phase 5 gates all passing.**
  - **Invisible character & recurring `GL_OUT_OF_MEMORY` (0x505) solved:** both were the same bug.
    `GL_DEBUG=1` + `DRAW_TRACE` + `--wrap=nouveau_bo_new` showed `nouveau_bo_new` failing with
    `ENOMEM` (`memalign` NULL, `unmap=5 MiB`, `fordblks=364 MiB`) on `16 MiB` (`4096×4096` BC7)
    sprite/UI/background atlases while smaller textures (`<= 2048×2048`, weapons/shields/staffs)
    still fit. Newlib `dlmalloc` on Horizon has `HAVE_MMAP=0`, so 540+ per-frame `8100 KiB`
    (`1920×1080×4`) Mesa staging BOs interleaved with small allocations and shredded 364 MiB of free
    heap into `< 8 MiB` holes.
    - Fixed with [src/nx/nx_large_arena.c](src/nx/nx_large_arena.c): dedicated `800 MiB` page-aligned
      best-fit coalescing arena for `512 KiB .. 128 MiB` allocations (`--wrap` of newlib's
      `_malloc_r`/`_calloc_r`/`_memalign_r`/`_realloc_r`/`_free_r`/`_malloc_usable_size_r`),
      isolating large GPU BOs from `dlmalloc`.
    - Tightened Switch `TextureCache` eviction (`UnusedFramesBeforeEviction = 300`, sweep every 64
      frames) plus emergency `Sweep(2)` + `glFinish` retry on `GL_OUT_OF_MEMORY`.
    - Verified through Create Character (`frame_02400.bmp`), Select Player (`frame_02640.bmp`),
      Rannie's Inn cutscene (`frame_02880..03120.bmp`), and into the Tutorial stage (`tutorial-verify`,
      3900 frames / 1.3M draws, `frame_03360..03840.bmp`): armored Fighter renders in full detail
      with lighting/HUD/checklist, `glErr=0x0(0)`, zero `NOUVEAU_BO_FAIL`, `heap_free=372–436 MiB`,
      `arena=568–614/800 MiB` (`big=65–125 MiB` contiguous free).
  - **Switch audio bug found & fixed:** devkitPro SDL2's `SWITCHAUDIO_PlayDevice` spins forever in
    `while (wavebuf.state != Playing)` if a >21 ms load hitch lets a wavebuf jump straight to `Done`,
    deadlocking audio and leaking `SDL_QueueAudio` at ~11 MiB/min. Replaced with a direct 6-slot
    `4096 B` `libnx audout` ring in `src/runtime_audio.c`: steady `938/937 buffers/5s`, `~42 ms`
    latency (`min queued 4–8 KiB`), zero starvation after boot, survives 390 ms load hitches.
  - **Phase 5 gates passed:** `BOX64_DYNAREC_STRONGMEM=1` (now default in `src/nx/box64_os_nx.c`,
    zero measured speed penalty), split CPK (`DRACRO.CPK.000/.001`), `SoftwarePageTable` run
    (`runs/20261122-222015-softpt-gate`, 1260 frames, `glErr=0x0(0)`), RAM margin (~390 MiB free),
    and **30-min soak** (`runs/20261123-082732-soak-30m`, 1800 s, 21,180 frames, 9,334,973/9,334,973
    draws, `glErr=0x0(0)`, `heap_free=389 MiB`, `arena=610/800 MiB` with `big=56 MiB` contiguous,
    steady `938/937 buffers/5s` `audout`, zero freezes/errors).
  - **Early exit at ~200 s explained:** `padexplore` accidentally opened the `SoftwareKeyboard`
    applet ("Dying message") at 2:40; Ryujinx Canary 1.3.354 ran 330 s cleanly with no exit.
  - **Phase 7 cleanup:** removed `R#17..60` read logs from `src/runtime_file.c` and the hardcoded
    `fs_mgr`/PEND probes from `gl_presenter.cpp`.
- **Earlier fixes** (details in §1.1):
  1. Box64 dynarec memprot registration via `nx_x64_mark_code` (`src/nx/box64_os_nx.c` / `src/loader.c`).
  2. Guest pool sized to 2176 MiB direct + 32 MiB flexible; render-target attachments guarded
     (`Rasterizer::AttachImage`).

### 0.0 FPS deep-dive (2026-11-23 evening)
**Hardware result (log 066, GLES path, tutorial gameplay, 74 windows):** 10.1 → **30.9 fps**
(`tot` 99.4 → 32.9 ms, `draw` 69.9 → 15.0, `cmt` 49.2 → 1.2, `cs` 1.2, `idle` 12.8) at ~661
draws/frame (was ~193); arena `big` 34–87 MiB (was 3). Locked at 2 vblanks: the GPU thread is busy
~20 ms/frame (draw 15 = shd 2.9 + stg 3.5 + vtx 1.8 + cmt 1.2 + gl 3.8 + rest; cs 1.2; ~4 PM4 +
present), just over 16.7 ms. 60 fps needs ~4 ms off the GPU thread and the guest under 16.7 ms.
**Run B (log 067, desktop GL 4.3 + persistent ring, 16 gameplay windows):** renders correctly,
30–49 fps, draw 12.6 ms (19.5 µs/draw vs 22.9 on GLES), same ~30 fps lock (`idle` 12.3), but
**the console crashed when the game was closed** (log ends cleanly at `PollEvents returned false`;
no Atmosphère crash report). GLES `nx.ini` restored; logs copied to `runs/hw-066-gles.log` and
`runs/hw-067-desktopgl.log`. Desktop GL stays an option once its exit path is fixed.

- **Hidden ~50 ms/frame found: per-frame display-buffer clear.** The game clears its 1920×1080
  display buffer to `0xff000000` with one compute dispatch per frame (`BufferFillShader`). The GL
  path emulated it as CPU `std::fill` (8160 KiB) **plus a full re-upload of the render-target
  texture** in `TextureCache::NoteGpuWrite` (`Upload`, up to 82 ms). Now
  `Rasterizer::ClearFilledImage` (`gl_rasterizer.cpp`) clears the GL texture with
  `glClearBufferfv` when the fill exactly covers one GPU-written 32-bit RGBA/BGRA 2D image and every
  byte is `0x00`/`0xff`; `NoteGpuWrite(addr, size, cleared)` skips its re-upload. Ryujinx: `cs`
  49 → 5.5 ms/frame (rest = the CPU fill), attract mode 12–15 → 21–27 fps. Frames verified. On
  hardware this is the most likely owner of the "unaccounted" ~29 ms in log 065.
- **HLE bridges now JIT-compiled.** The bridge arena (`make_bridge`, `box64_os_nx.c`) was never
  R+X in Box64 memprot, so every HLE call (incl. memcpy/memset/mutex) left the JIT, took
  `mutex_dyndump` twice and ran INT3+RET in the interpreter (~480k/s). Registered in
  `box64_process_init`; `STALL_DUMP` `interp` count went from ~2.5M to ~0–100 per 5 s.
  Kill switch: `NX_BRIDGE_JIT=0`.
- **Hot-path cleanups:** patched Box64 DynaRun counters only count under `STALL_DUMP`
  (`nx_dyn_stats_on`); `EmuInt3` stops its worker-trace `strcmp` chain after 150 lines;
  `sysv_wrapper` caches a readable stack-top page per thread instead of `svcQueryMemory` per call.
- **Profiler line now complete:** `Presenter: show` adds `cs=ms(dispatches)`, `idle=` (GPU thread
  waiting for guest submits = guest-CPU bound) and `vo=` (GPU thread waiting on the flip label /
  60 Hz tick). `tot ≈ draw + cs + swap + idle + vo + small`.
- **Startup deadlock found and fixed (Ryujinx, 4 of 8 runs):** `RenderDLThread` stuck in
  `sceGnmSubmitCommandBuffers` with `submission_lock=1` while the GPU thread slept idle with
  nothing queued (lost `GpuIdle` wakeup; protocol is correct on paper, so likely the condvar layer
  under Ryujinx). `WaitGpuIdle` (`gnmdriver.cpp`) now re-checks every 100 ms and drops a lock the
  retired GPU left behind (logs `GnmDriver: stale submission lock dropped` once). Diagnosed with new
  `STALL_DUMP` lines: `GPU: <phase> arg= for Nms; submission_lock= gpu_idle=` (GPU thread phase:
  idle/pm4 opcode/command/vo_label/wait_reg_mem/submit_end) and `IN call[subphase]`.
- **Hardware ran the GLES path:** SD `nx.ini` has `GL_MODE=gles` (ANGLE-oriented: client-memory
  vertex arrays re-uploaded by Mesa every draw, shader ring via texture). Ryujinx always ran desktop
  GL 4.3 core with the persistent ring. The `Mapped` TBO ring for GLES now validated on Ryujinx
  (`cmt=2–3 ms`). No record why GLES was chosen on hardware.
- **New knob:** `NX_GPU_THREAD=prio[:core]` (e.g. `44:2`) moves the GPU command thread; it now
  always logs `Sched: host 'GpuCommandProcessor' ...` (it ran at prio 59 like every guest thread).
- **Box64 patch** regenerated as UTF-8 with a header (the old one was UTF-16LE + CRLF, which
  `git apply` rejects).

**Census run (log 068 via bringup.log, GLES, 30 fps windows), per frame:** GPU thread 55% CPU
≈ 18.5 ms (the only stage over 16.7 ms), `RenderDLThread` ~50% (incl. `scePthreadYield` spin),
`main` ~43%, CRI Server Manager ~11%, workers ~2% each; total ~176% of 300%; `gw=0`, `vbw` 10–20.
New line fields: `pres` (PrepareFrame+Show−swap), `gw` (guest in WaitGpuIdle), `vbw` (guest in
WaitVblank), `heal=` (stale locks dropped), `CPU:` line per 60 frames (Horizon `ThreadTickCount`).

**Toward 60 (built, Ryujinx-verified, hardware pending):**
- GLES rings now `Persistent` via `GL_EXT_buffer_storage` (`glBufferStorageEXT` loaded in
  `gl_api.cpp`, detected in `QueryGlesCapabilities`): real VBO/IBO + shader TBO mapped once, no
  Mesa client-array upload, no per-draw map/unmap. Kill switch `NX_PERSISTENT_RING=0`. Fixed the
  GLES index scan that read the ring's CPU copy (`PointerBase()+offset`, null for buffer rings) →
  scans the guest source instead.
- CPU fill of the display buffer skipped when its sampled hash still matches the last fill
  (`last_fills`): `cs` 5.5 → 0.3 ms on Ryujinx.
- GPU thread idle wait re-checks every 5 ms (`liverpool.cpp`); `WaitGpuIdle` self-heal timeout
  100 → 5 ms. One Ryujinx run healed every frame (`heal=60`), the next none (`sets=60`,
  `resets=idles`): timing-dependent, now bounded at ≤5 ms.

**Hardware log 069 (GLES + persistent rings):** gameplay 30.9 → **40.1 fps** avg (`draw` 12.8,
`cmt` 0, `cs` 0.2, ~20 µs/draw); ≤~600 draws/frame now runs 45–57 fps, ≥~650 still 30. Clean exit.
GPU thread remains the gate (~63% CPU); `RenderDLThread` 52–69%, `main` 40–58%.
- Mesa `glthread` is compiled into switch-mesa but nothing calls `_mesa_glthread_init` (dead), and
  nouveau has no Gallium threaded context: no driver-side offload possible.
- Next build (deployed, hardware pending): flat user-data + clip-plane pushes deduplicated by content
  (`PushGuest`), so the per-stage buffer table stops changing (and re-uploading a uniform) every draw;
  overlapping vertex attributes of one buffer pushed once (`BindVertexInputs` runs). Ryujinx: 38–45
  → 32–35 µs/draw in the same scene, frames verified.

**Hardware log 070:** the flat/clip dedup and vertex-run merge gave no measurable change (21.1 vs
20.4 µs/draw, run-to-run noise); the vertex-run merge was reverted, the two-line dedup kept.

**Exit crash fixed (hardware-verified, log 071: `Switch: quitting` logged, no new crash report):** closing the game aborted the loader (Atmosphère
crash report: process `Forwarder` 0100444f41350000, module `game_host`, User Break, our module
already unmapped). Cause: `std::_Exit(0)` on the window thread returns to the NRO loader while
guest/GPU/audio threads still run in our code. Now `nx_platform_quit()` (`nx_platform.c`, called
from `dcpgpu.cpp`) restores stock clocks (CPU 1020, GPU 307.2/768, EMC 1331.2/1600 MHz — they
used to stay boosted after quitting) and calls `svcExitProcess()`.

**Controls (devkitPro SDL2 "Switch Controller" mapping is by label, `runtime_pad.c`):** A = Cross,
B = Circle, X = Square, Y = Triangle, L/R = L1/R1, ZL/ZR = L2/R2, stick clicks = L3/R3, **+ = Options
(pause/menu)**, − = touchpad click, L3+R3 together = the port's Options overlay. Remap per button with
`PAD_MAP=cross=b,circle=a` etc. in `nx.ini`.

### 0.1 Remaining items (Switch hardware to-do)
1. **Hardware testing (`hw-072` -> `hw-073` + Round 4 deployed via FTP):**
   - `hw-072` (`runs/hw-logs/dragons_crown_pro_072.log`): `OpenGL ES 3.2 Mesa 26.2.4 (git-0ff52079ca)` (`MESA_SWITCH_GLTHREAD` defaults `true` in `egl_switch.c`) jumped to `37.9-55.4 fps` (`draw=5.6-7.4 gl=0.1-0.2`) but hit `resets=0 heal=60 gw=5.0 ms` and Mesa GLSL cache deprecation warning. Fixed `Common::Singleton<T>::Instance()` race (`std::call_once` in `gpu/shadps4/common/singleton.h`), moved `Register(GpuIdle, ResetSubmissionLock)` before `liverpool` (`gnmdriver.cpp`), switched to `MESA_SHADER_CACHE_DIR` / `MESA_SHADER_CACHE_DISABLE` (`nx_platform.c`).
   - `hw-073` (`runs/hw-logs/dragons_crown_pro_073.log`): `heal=0 sets=60 resets=120 idles=120 gw=0.0 ms`, `52.3-58.1 fps` (`tot=17.2-18.4 ms`, `draw=5.3-6.3 ms`) at `~600-650` draws/frame, `glErr=0x0(0)`. Remaining bottlenecks: `mode=off` core 1 contention, `pres=3.6-5.8 ms` ImGui state queries, `ref=1.5-1.9 ms` hashing.
   - **Round 4 fixes (`hw-074`, `runs/hw-logs/dragons_crown_pro_074.log`):** default `DCP_NX_SCHED=spread` + default `GpuCommandProcessor` core 2 (`src/nx/nx_platform.c`), lockless ImGui (`IMGUI_IMPL_OPENGL_NO_STATE_BACKUP` + `PersistentVao`), `HashTextureContent()` across `Refresh`/`MarkTarget`/`NoteGpuWrite` (`gl_texture_cache.cpp`). On Switch HW (`hw-074`), `pres` dropped `3.6–6.6 ms` → **`0.7 ms`**, `ref` `1.6–1.9 ms` → **`0.2–0.3 ms`**, `draw` **`4.3–5.2 ms`** (`57.1–58.2 fps`, `tot=17.2–17.5 ms`, `idle=4.6–7.5 ms`, `swap=2.2–5.0 ms`, `gw=0.0`, `heal=0`, `glErr=0x0(0)`).
   - **Round 5 fixes (`hw-075`, `runs/hw-logs/dragons_crown_pro_075.log`, handheld `gpu=460MHz`):** removed per-60-frame `fsync(1)` + duplicate `Presenter: frame` log, combined `Presenter: show` + `CPU:` (`nx_platform_cpu_report_str`), skipped ANGLE-only CPU `HighestElementInside`/`HighestIndex` scans when `ring.Persistent()`, 1-entry MRUs for `FramebufferFor`/`SamplerFor`. On Switch HW (`hw-075`): hit `60.0 fps` peak (`#5040`) and `54.7 fps` avg across 96 samples (`draw=4.1–5.8 ms`, `pres=0.4–0.6 ms`, `ref=0.2–0.3 ms`, `gw=0.0`, `heal=0`, `glErr=0x0(0)`), but revealed a ~300-frame (5 s) `swap` sawtooth (`1.1 -> 7.3 ms`) beating against `nwindowSetSwapInterval(1)` (`MaxPendingShows=2` drops to `51.4–57.9 fps`) plus ~10–25 ms SD stalls from synchronous `bringup.log` `fopen/fclose`.
   - **Round 6 fixes (verified on Ryujinx `opt6-ryu`, pushed via FTP SHA-256 `613A1189...`):** default `vsync=false` on `__SWITCH__` (`dcp_settings.h`, `DCP_VSYNC` / `VSYNC=` override in `gl_presenter.cpp`); `PresentThread` prio `40` (`SetCurrentThreadRealtime` -> `nx_platform_set_realtime_thread` in `thread.cpp`); `GpuCommandProcessor` prio `48` (`nx_platform.c`); async `nx_trace_worker` prio `61` writing `bringup.log` off the GPU thread (`nx_platform.c`).
   - **Round 7 in-game Switch menu (verified on Ryujinx `opt7-final`, pushed via FTP SHA-256 `9D43CDD5...`):** `L3 + R3` opens directly on a new `Switch` (`nx.ini`) tab (`dcp_options_menu.{h,cpp}`) backed by `nx_platform_save_ini` (`nx_platform.{h,c}`), live `NX_BOOST` (`nx_platform_set_boost`), live `VSYNC`, live `PAD_MAP` (`runtime_pad_set_map`), and live `FRAME_DUMP` (`gl_presenter.cpp`), plus `(restart)` keys (`NX_SAFE`, `GL_MODE`, `AUDIO`, `NX_SCHED`, `NX_GPU_THREAD`, `NX_PERSISTENT_RING`, `STALL_DUMP`, `GL_DEBUG`).    - **Round 8 Switch HOME+Close exit fix (`hw-076`/`hw-077` → `opt8-exitfix`, pushed via FTP SHA-256 `55B44968...`):** `hw-076` (`57.3–60.2 fps`) and `hw-077` (`58.9–59.8 fps` steady `#2640–#3180`, `pres=0.2–0.6 ms`, `glErr=0x0(0)`) confirmed perf; updated `nx_platform_quit()` (`nx_platform.c`) to restore clocks, call `nx_platform_exit()` (flush trace + `appletUnlockExit()` + `socketExit()`), set `__nx_applet_exit_mode = 1`, run `appletExit()`, and enter libnx's `_appletExitProcess` (`envGetExitFuncPtr()`), plus defined `userAppExit()`. **Verified clean HOME → Close shutdown on Switch HW (`hw-077`, no error popup)** and boot/render on Ryujinx `opt8-exitfix`.
   - `opt6-ryu` (`20261124-125601`, 90s GLES `NX_SAFE=0,FRAME_DUMP=600`): `GpuCommandProcessor core 2 prio 48`, `VSync off`, `#2820` `draws=216251/218979 center=(238,163,87) max=238 glErr=0x0(0)` (`#2220 fps=60.0`, `pres=0.4`, `ref=0.0–0.1`).
2. **Completed before HW A/B (verified on Ryujinx Canary 1.3.354):**
   - Built `mesa-switch` 26.2.4 OpenGL SDK (`../mesa-switch/build-opengl.sh` → `mesa-install/opt/devkitpro/portlibs/switch`) and linked via `CMakeLists.txt` / `gpu/CMakeLists.txt` (`DCP_MESA_SWITCH=1`); added Horizon VA non-fixed `AllocSpace` fix (`../mesa-switch/src/nouveau/horizon/nouveau_horizon_{private.h,vm.c}`) for Ryujinx `MapBufferEx` `0x195c`.
   - Forwarded `MESA_*`/`NOUVEAU_*`/`LIBGL_*`/`MESA_SWITCH_GLTHREAD`/`mesa_glthread` in `src/nx/nx_platform.c`.
   - Implemented GPU & CPU speedups: `IrqController` `std::array` (`core/platform.h`), `m_wait_idle` lock in `sceGnmSubmitDone` (`gnmdriver.cpp`, `heal=0`), zero-copy `ParseFetchShaderPtr` + `tsl::robin_map` + MRU (`fetch_shader.{h,cpp}`), `StageSpecialization::Matches(..., check_runtime)` (`specialization.h`), 64-slot `FastGraphicsSlot` + `tsl::robin_map` in `ShaderCache` (`gl_shader_cache.{h,cpp}`), `ClipPlanes` cache + intra-draw `pushed_slices` reuse (`gl_rasterizer.{h,cpp}`, `gl_stream_ring.h`), skip depth guest hashing (`gl_texture_cache.cpp`), gated 5× `glReadPixels` (`gl_presenter.cpp`), templated 1D/split-tile `Detile` (`gl_detile.cpp`), `nx_x64_register_code_range` + `BOX64_DYNACACHE=0` (`box64_host_nx.c`/`box64_os_nx.c`), adaptive `thread_yield()` backoff (`runtime_thread.c`).
   - Ryujinx results (`NX_SAFE=1, NX_SCHED=spread`):
     - GLES attract (`#2580`): baseline `glthread=0` (`20261123-225241`) `fps=52.8 draw=8.6 gl=3.3 CPU 138-205%` → final `glthread=0` (`20261123-234049`) `fps=59.0 draw=4.8 gl=1.8 CPU 72-92%` → final `glthread=1` (`20261123-234517`) `fps=60.0 tot=16.7 draw=2.2[shd=0.6 stg=0.5 vtx=0.6 gl=0.0] CPU 53-64%`.
     - GLES 120s pad tavern (`~525` draws/frame, `Select player`): pre-`FastGraphicsSlot` (`231730` `#2700`) `fps=45.5 draw=9.6[shd=3.3]` → final (`233552` `#3780..#4080`) `fps=50.7-52.2 draw=8.5-8.7[shd=2.4]`, `glErr=0x0(0)`.
     - Desktop GL 4.3 Core 120s pad (`20261123-233823`, hero creation `#4920`): `fps=59.0 tot=16.9 draw=5.2[shd=1.5 stg=1.4 vtx=1.3] draws=1231054/1231054 glErr=0x0(0)`.
3. **Skip the redundant CPU fill** (`cs` remainder, 2–5 ms on HW if still noticeable) and **pad mapping check** (`PAD_MAP=cross=b,circle=a` polarity on hardware).
4. **Commit** `switch-port` and `mesa-switch` edits once hardware A/B finishes (evaluate NVK + shadPS4 Vulkan backend if still below locked 60 fps on Switch HW).

### 0.2 Diagnostics kept behind `nx.ini` knobs
- `GL_DEBUG=1`: KHR_debug synchronous callback (`GL_DBG` in `bringup.log`).
- `DRAW_TRACE=N`: dumps every draw of presented frame `N` to `logs/draw_trace.txt`, unique linked
  GLSL shaders to `logs/draw_shaders.txt`, and `logs/frame_NNNNN.bmp`.
- `FRAME_DUMP=N`, `STALL_DUMP=1`, `AUDIO_STATS=1`, `NX_LARGE_ARENA_MB=N`, `PAD_FILE=...`.
- Always-on zero-cost failure logs: `GL_ALLOC_ERR` (texture allocation error) and `NOUVEAU_BO_FAIL`
  (`--wrap=nouveau_bo_new`, logs only when `rc != 0`).

## 1. Stall analysis (historical, solved)

### 1.1 Root cause
`STALL_DUMP=1` showed every CRI thread idle except `CRI FS Data Decompression 0`, which stayed in guest
code with zero HLE calls for minutes; sampled registers showed it in the CRILAYLA loop (`0x62cfd0`)
with the output pointer advancing ~24 KB/s. Dynarec and interpreter runs had identical timings, and a
counter in `DynaRun` showed `blocks +0 interp +1.5M` per 5 s: the dynarec refused every block because
`FillBlock64` requires `PROT_READ|PROT_EXEC` in Box64's memprot map. The analysis below was written
before this was found; H1 (scheduler) was tested and refuted (guest threads already run at priority
59 spread over cores 0–2).

### 1.2 Original analysis

Timeline (`bringup.log`, 2026-11-22 17:52): `R#17` @frame 9 → `R#18` @60 → `R#19` @120 → `R#20..27` by
180 → `R#28..42` by 240 → none until 300 → `R#43..47` by 360 → none after. PC creates its `'guest'`
workers (`entry 0x5a1930`) in a burst; Switch creates one, it exits, about 1 s later the next.

One step per ~1 s is a platform signature: waits ending only by a 1 s timeout, or threads starved until
something sleeps. A wait with no timeout under the same fault gives the permanent `pending=4`. The guest
code and runtime are identical to PC, so the bug is in the Switch delta. Ranked hypotheses:

1. **H1 scheduler starvation.** Guest threads are plain libnx `pthread_create` threads: inherited
   priority, default core, no affinity. Likely all ~25 on core 0 at one priority, cores 1–2 idle. Horizon
   does not time-slice equal priorities (only priority 59 on cores 0–2 rotates every 10 ms), so pollers
   (`CRI Server Manager`, main thread `sceKernelPollEventFlag`) starve CRI FS / decompression threads.
2. **H2 libnx/newlib sync semantics.** `pthread_condattr_setclock(CLOCK_MONOTONIC)` (`runtime_sema.c`,
   `runtime_thread.c`), `PTHREAD_MUTEX_ERRORCHECK` (default PS4 type; newlib only has normal/recursive
   storage), cond wait on recursive mutexes. Never tested on libnx.
3. **H3 I/O content.** Byte counts match; bytes never verified. Ryujinx does not need split files.
4. **H4 guest clocks.** Box64 `RDTSC` vs HLE `sceKernelGetTscFrequency` (calibrated from host CNTPCT)
   mismatch would slow CRI's 60 Hz server tick to ~1 Hz.
5. **H5 Box64 CPU bug** in decompression. Less likely: `DYNAREC=0` behaves the same.

Game-side detail (from static analysis of `boot.bin`, kept for reference):
- Main loop `0x49940 → 0x5687c0 → 0x5a1f60 → 0x56fb80 → 0x58def0`. Boot task `0x16b30` (update
  `0x16d40 → 0x16da0`, 10-state table `0x644c80`).
- State 2 (`0x17006`) queues `SND/SE_COMMON.ACB`, `SE_STAGE.ACB`, `BGM.ACB`, `BGM.AWB`, waits at
  `0x17082` for `[fs_mgr+0x78] == 0` (`fs_mgr = [[image+0xe78708]+0x48]`). State 3 (`0x17091`) queues 13
  files, waits at `0x171d4`, queues 42 more. State 4 (`0x17327`) calls `0x6df40(0x1807a444)` =
  `sceFontOpenFontSet`.
- At frame 240, 7 entries were in `b5b=1,b5c=1` (`DEFAULTBONE_US.FMS`, `ONMEMORYEFFECT01.MCB/.MBS`,
  `INTERFACE_US.MBS/.FTX`, `FONTUTF16.FTX`, `SYSTEMMSG_US.FMS`); 3 finished later, 4 stay in
  `FileEntry::Update` (`0x57b850 → 0x57bb00 → 0x57bc67`) polling `[cri_loader+0x5b]` for 4 or 5.

### Lessons from the sessions so far

- Bisect the platform, don't reverse-engineer the game: PC boots the same code, so A/B toggles on the
  Switch delta (scheduler, sync, split I/O, clocks, Box64) find the cause faster than disassembly.
- Prefer generic probes ("which thread waits on what") over one-off hardcoded ones (`R#17..60`,
  frame-240 dumps, game-struct offsets in `gl_presenter.cpp`).
- Fixed-length, scripted Ryujinx runs with one log folder per run (`tools/ryu_run.ps1`).
- Commit before experiments; keep `patches/box64-switch.patch` current (it was stale: 1 of 6 files).

## 2. Plan (Ryujinx first)

### Phase 0 — Hygiene (done 2026-11-22)
- Port committed on `switch-port`; Box64 patch regenerated (6 files, upstream commit in its header);
  parent-folder logs/slices archived; handoffs merged.

### Phase 1 — Repeatable Ryujinx loop (done)
- `powershell -ExecutionPolicy Bypass -File tools\ryu_run.ps1 -Label x -Seconds 240 -StopMarker NEVER
  -Ini 'KEY=V,KEY2=V2'` → build, deploy, run, logs + frame dumps in `runs\<ts>-<label>\`, summary line.
  `-NoBuild` skips the build; the default stop marker is `Font: system font set`.
  `-Pad "55:options,70:options,85:cross"` presses pad-1 buttons at seconds after launch (PS4 names:
  cross, circle, square, triangle, l1, r1, l2, r2, l3, r3, options, touchpad).
- Launching Ryujinx by hand also works, but logs stay in the SD `logs/` folder and `bringup.log`
  keeps growing; `nx.ini` keeps whatever the last `ryu_run` wrote.
- Timing reference (default build, no input): entry ~11 s, CRIWARE ~25 s, game logo ~49 s, title
  ~119 s after process start.

### Phase 2 — Platform instrumentation (done; nx.ini knobs)
- `STALL_DUMP=1`: every 5 s, dynarec block/interpreter counts plus per-thread current HLE call, args,
  time in call, call rate; threads in guest code without HLE calls get 6 register samples.
- `FRAME_DUMP=N`: every N-th presented frame (multiple of 60) as a quarter-size BMP in `logs/`.
- Thread census `Sched:` lines; `NX_SCHED=spread|mapped` (no effect on the old stall; keep for HW).
- `BOX64_*` keys pass through to Box64 unchanged. bringup.log lines carry ms timestamps; the presenter
  line reports `heap_free`.
- Not done (not needed so far): sync ledger, sync/clock self-tests, PC baseline diff, CRC read trace.
- Still to clean: hardcoded `R#17..60` read logs, `fs_mgr`/PEND probes in `gl_presenter.cpp`.

### Phase 3 — A/B experiments (done)
- Scheduler spread: no change. Dynarec counters → root cause (§1.1).

### Phase 4 — Ryujinx milestones
- Done: `pending=0`, `Font: system font set`, `SAVEDATA00` write, logos/title (`max>0`), input on
  title, menus, character creation, tutorial stage loads.
- Next: §0.1 (invisible character first) → first real stage → 30-min soak.
- `NX_SAFE=0` features one by one later (needs >4-arg/XMM host→guest marshalling, §7).

### Phase 5 — Ryujinx gate before hardware (done 2026-11-23)
- `MemoryManagerMode` SoftwarePageTable (`softpt-gate`, 1260 frames, `glErr=0x0`),
  `BOX64_DYNAREC_STRONGMEM=1` defaulted on, RAM margin (`heap_free=372–436 MiB`,
  `arena=568–614/800 MiB`), split CPK (`DRACRO.CPK.000/.001`), direct `libnx audout` stereo audio,
  and 30-min soak (`soak-30m`, 21,180 frames, 9.33M draws, `glErr=0x0`).

### Phase 6 — Hardware
- `tools/prepare_nx.ps1 -SplitCpk` to SD, same `nx.ini` as the passing Ryujinx run, Title Takeover
  (hold R on a game), logs via FTP (`ftp://192.168.8.221:5000/switch/dragons_crown_pro/`).

### Phase 7 — Cleanup (done)
- Removed `R#17..60` read logs from `src/runtime_file.c` and `fs_mgr`/PEND probes from
  `gl_presenter.cpp`; kept `GL_DEBUG`, `DRAW_TRACE`, `FRAME_DUMP`, `STALL_DUMP`, `AUDIO_STATS` behind
  `nx.ini` knobs and `g_trace_lock` in `nx_platform.c`.

## 3. Architecture summary

The PC port is not a C++ source port: it is a native x86-64 PS4 ELF loader (`src/loader.c`,
`DCPBOOT1` boot image) that maps `eboot.bin` + PRXs into memory and serves PS4 HLE calls from
`src/runtime_*.c`, with a shadPS4-derived GPU core (`gpu/`).

Switch port choices:
- CPU: Box64 (ARM64 dynarec with W^X `jitCreate` pool, 48 MiB; interpreter via `DYNAREC=0`) with
  `SysV -> AAPCS64` host thunks (`src/nx/nx_sysv_call.S`).
- GPU: `renderer_opengl` via SDL2 + EGL on the Switch Mesa/nouveau driver (desktop GL 4.3 core).
- Storage: PS4 paths mapped to `sdmc:/switch/dragons_crown_pro/...`, split-file support for FAT32.
- SDL: SDL3 API shim over the devkitPro SDL2 port.

## 4. Switch runtime model

- **Guest virtual addresses are host pointers.** `runtime_memory.c` `place()` returns
  `backing_base + phys` for direct/flexible mappings. Reservations own a real block that a commit reuses
  in place.
- **Page protection is a no-op.** `GuestWriteWatch::TryArm` returns `nullopt` on Switch; the GL texture
  cache compares content every frame.
- **Fibers** own a host stack, a guest stack and their own `NxX64FiberEmus` array.
- **Safe mode is the default.** `NX_SAFE=1` skips all guest-hook features and clears the GPU feature
  bits. `NX_SAFE=0` enables them.
- **`nx.ini`**: each `KEY=VALUE` line becomes `DCP_KEY`. Keys: `NX_SAFE`, `NX_DMEM_MB`, `NX_FMEM_MB`,
  `AUDIO`, `PAD_MAP`, `GPU_PROBE`, `X64_SELFTEST`, `CPU_ONLY`, `CHECK_IMPORTS`, `DYNAREC`, `TRACE_HLE`,
  `STALL_DUMP`, `FRAME_DUMP`, `NX_SCHED`; `BOX64_*` keys are also passed to Box64 under their own name.
- **Pool defaults**: 2176 MiB direct + 32 MiB flexible (override with `NX_DMEM_MB` / `NX_FMEM_MB`).
- **Split files**: a missing `NAME` opens `NAME.000`, `NAME.001`, ... as one read-only file. The base
  file wins if present.
- **Logs**: `logs/bringup.log` (early stages, every `fail()`, presenter milestones) and the normal
  runtime log `logs/dragons_crown_pro_NNN.log`.
- **Clocks**: `rdtsc` uses `CNTPCT_EL0` (Horizon traps EL0 `CNTVCT_EL0`).
- **Audio**: 8ch float downmixed to stereo `AUDIO_S16LSB`.

## 5. Test harness and SD layout

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
- `tools/build_nx.ps1` — build the `.nro` (devkitPro bash: `aarch64-none-elf-cmake` +
  `cmake --build build-nx`).
- `tools/prepare_nx.ps1` — copy/flatten the PC game folders, write `.nro` and `nx.ini`; `-SplitCpk`.
- `tools/split_cpk.ps1` — split files larger than the part size (default 3900 MiB).

Startup tests (exit after printing their result): `GPU_PROBE=1`, `X64_SELFTEST=1`, `CPU_ONLY=1`,
`CHECK_IMPORTS=1`.

Ryujinx: `%USERPROFILE%\Downloads\ryujinx-1.3.3-win_x64\publish\Ryujinx.exe`, game files under
`%APPDATA%\Ryujinx\sdcard\switch\dragons_crown_pro\`, `.nro` targeted on the command line.

Hardware: launch in **Title Takeover** (hold R on a game icon), not from the album applet.

## 6. Files added/changed (this port)

New: `src/nx/` glue, `tools/build_nx.ps1`, `tools/prepare_nx.ps1`, `tools/split_cpk.ps1`,
`patches/box64-switch.patch`, `gpu/shim/nx_vk_stubs.cpp`, `packaging/nx/README.txt`.

Modified: `CMakeLists.txt`, `gpu/CMakeLists.txt`, `src/loader.c`, `src/runtime*.c`, `src/guest_hook.c`,
`src/isa_emulate.c`, `launcher/paths.cpp`, `gpu/shim/*`, `gpu/shadps4/video_core/renderer_opengl/*`,
`gpu/shadps4/video_core/renderer_factory.cpp`, related common/video files.

Diagnostics in the tree (temporary): `nx_platform_trace` (+ `g_trace_lock`, keep), HLE/`W<id>`/`L`
traces in `box64_os_nx.c`, worker-id in `runtime_thread.c`, `g_nx_image_base` in `loader.c`, `R#17..60`
read logs in `runtime_file.c`, `pending=`/PEND probes in `gl_presenter.cpp`.

## 7. Known issues / limitations

- The host-side CPK reader used for text/appearance/voice scans expects one whole `DRACRO.CPK`; those
  scans do not work on a split card. The game's own reads do.
- `getdents` lists `DRACRO.CPK.000/.001` rather than the virtual base name.
- Host->guest calls marshal at most four integer arguments and RAX (no XMM args or returns).
- `third_party/box64` is git-ignored; `patches/box64-switch.patch` is the source of truth. Regenerate
  with `git -C third_party/box64 diff --ignore-cr-at-eol` after Box64 edits.

## 8. Reproduction

```powershell
powershell -File tools\build_nx.ps1
powershell -File tools\prepare_nx.ps1 -Destination C:\local\switch\dragons_crown_pro -SplitCpk
nxlink -s -a <PC-IP>
```
