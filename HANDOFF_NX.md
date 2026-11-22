# Dragon's Crown Pro — Nintendo Switch (`dragons-crown-pro-nx`) — Status & Plan

Last updated: 2026-11-22. Branch: `switch-port`. This is the only handoff document
(the former `..\handoff.md` is merged here and archived in `..\_archive\2026-11-22\`).

## 0. Status

- **Goal:** full playable build in Ryujinx (Canary 1.3.354), then hand over to Switch hardware.
- **Build:** `build-nx/dragons_crown_pro.nro` builds cleanly (`tools/build_nx.ps1`), synced to
  `%APPDATA%\Ryujinx\sdcard\switch\dragons_crown_pro\dragons_crown_pro.nro`.
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
    (`runs/20261122-222015-softpt-gate`, 1260 frames, `glErr=0x0(0)`), RAM margin (~390 MiB free).
  - **Early exit at ~200 s explained:** `padexplore` accidentally opened the `SoftwareKeyboard`
    applet ("Dying message") at 2:40; Ryujinx Canary 1.3.354 ran 330 s cleanly with no exit.
  - **Phase 7 cleanup:** removed `R#17..60` read logs from `src/runtime_file.c` and the hardcoded
    `fs_mgr`/PEND probes from `gl_presenter.cpp`.
- **Earlier fixes** (details in §1.1):
  1. Box64 dynarec memprot registration via `nx_x64_mark_code` (`src/nx/box64_os_nx.c` / `src/loader.c`).
  2. Guest pool sized to 2176 MiB direct + 32 MiB flexible; render-target attachments guarded
     (`Rasterizer::AttachImage`).

### 0.1 Remaining items (before / on Switch hardware)
1. **30-min soak** in Ryujinx (or manual play into first real dungeon stage + boss) to confirm long-run
   arena stability and zero intermittent 17 s stall (not reproduced across 8 consecutive Canary 1.3.354
   runs).
2. **Real Switch hardware boot** (Title Takeover, hold R on a game): deploy with
   `tools/prepare_nx.ps1 -SplitCpk`, verify visual output, `libnx audout` audio, FPS, and
   `heap_free`/`arena` in `logs/bringup.log`.
3. **Pad mapping (hardware check):** confirm whether Switch physical B arrives as Cross (positional)
   or A (label); adjust `PAD_MAP=cross=b,circle=a` in `nx.ini` if inverted, then make it default.
4. **C++ throw in Ryujinx (known Ryujinx quirk, non-hardware):** libgcc's unwinder executes 5
   `chkfeat x16` (`0xd503251f`) before `mrs gcspr_el0`; Cortex-A57 hardware treats `chkfeat` as NOP.
   Only needed if a Ryujinx test hits a C++ throw.
5. **`NX_SAFE=0` features** (later): guest-hook features need >4-arg/XMM host→guest marshalling (§7).

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

### Phase 5 — Ryujinx gate before hardware (done except 30-min soak)
- Done: `MemoryManagerMode` SoftwarePageTable (`softpt-gate`, 1260 frames, `glErr=0x0`),
  `BOX64_DYNAREC_STRONGMEM=1` defaulted on, RAM margin (`heap_free=372–436 MiB`,
  `arena=568–614/800 MiB`), split CPK (`DRACRO.CPK.000/.001`), direct `libnx audout` stereo audio.
- Remaining: 30-min soak (or manual play into first real dungeon stage).

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
