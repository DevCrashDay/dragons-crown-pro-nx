Dragon's Crown Pro — Nintendo Switch (dragons-crown-pro-nx)
============================================================

SD card layout (exFAT recommended)
----------------------------------
/switch/
  dragons_crown_pro.nro        this build
  dragons_crown_pro/
    nx.ini                     optional settings (see below)
    game/
      boot.bin                 DCPBOOT1 image made by the PC launcher
      content.bin  image.json
      eboot.bin  DRACRO.CPK  DRACRO_US.CPK
      sce_module/  sce_sys/
    user/
    logs/

tools\prepare_nx.ps1 -Destination <drive>:\switch\dragons_crown_pro copies all of
this from the PC build folder and writes the .nro.

FAT32 cards
-----------
FAT32 cannot hold DRACRO.CPK (5.26 GB). Split it into parts smaller than 4 GiB:

  powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Destination <local>\switch\dragons_crown_pro -SplitCpk

then copy the resulting folder to the card. The game folder then contains
DRACRO.CPK.000 and DRACRO.CPK.001 (no DRACRO.CPK); the runtime opens the base
name and reads the parts as one file.

To split an existing folder instead:

  powershell -ExecutionPolicy Bypass -File tools\split_cpk.ps1 -Path <...>\game

Each part must stay under 4 GiB. The splitter removes the source file unless
-KeepSource is passed.

Run
---
Use Title Takeover (not applet mode): the game allocates about 2.1 GiB of direct
memory before its first frame, and the default guest pool is 2304 MiB direct +
448 MiB flexible.

For logs, start `nxlink -s -a <PC-IP>` before launching the .nro.

nx.ini
------
Each KEY=VALUE line becomes the environment variable DCP_KEY.

  NX_SAFE=1          default: skip guest-hook features (cheats, boss battle,
                     appearance, online, voice, skip-intro, gallery,
                     resolution patch, ultrawide, text patch). Set 0 to enable.
  NX_DMEM_MB=2304    guest direct-memory pool (512..2560)
  NX_FMEM_MB=448     guest flexible-memory pool (128..1024)
  AUDIO=none         disable SDL audio
  PAD_MAP=...        remap PS4 buttons
  GPU_PROBE=1        graphics-only test: context, GL strings, one shader, 60 frames
  X64_SELFTEST=1     Box64-only test: arithmetic, fs: TLS, bridge, nested call
  CPU_ONLY=1         skip the GPU (loader/Box64 tests; GPU calls are not served)
  CHECK_IMPORTS=1    list every PS4 import with no HLE implementation, then exit
                     (add CPU_ONLY=1 in an emulator, where the GPU cannot start)

The same tests can be selected by creating gpu_probe.flag or x64_selftest.flag
in the dragons_crown_pro folder. Both tests exit after printing their result;
remove the setting/flag to boot the game.

Diagnosing an immediate return to hbmenu
----------------------------------------
Every run appends to logs/bringup.log, even without nxlink:

  --- bringup ... | applet=0 | RAM=3285 MiB ---
  Switch: nxlink fd=-1
  Switch: init done
  main: start argc=0
  Paths: ...
  Log: ...
  Boot: DCPBOOT1 ok, ...
  Memory: reserving ...
  Memory: ready: 2304 MiB direct + 448 MiB flexible
  GPU: initializing ...
  ...
  Entry: entering guest ...

If the file is missing, the app died before its own init (wrong layout, not
launched from the SD, or the .nro was not replaced). If it stops at "reserving"
or says "applet=1", launch in Title Takeover (hold R on a game icon in hbmenu),
not from the album applet.

Expected bring-up order
-----------------------
1. GPU_PROBE=1   -> "OpenGL ES: vendor ...", extension list, "GPU probe: ok"
2. X64_SELFTEST=1 -> four "ok" lines, "x64 selftest: ok"
3. neither       -> safe-mode boot; the nxlink log shows imports, memory pool,
                    module initializers and "Entering original x86-64 code".
