<div align="center">

# Dragon's Crown Pro - Nintendo Switch Port

### Vanillaware's PS4 action RPG running natively on Nintendo Switch via Box64 (ARM64 dynarec), shadPS4-derived OpenGL/GLES 3.2 video core, and Mesa 26.2.4

![Platform](https://img.shields.io/badge/platform-Nintendo%20Switch%20(Homebrew%20%2F%20CFW)-e60012?style=for-the-badge&labelColor=07101f)
![License](https://img.shields.io/badge/license-GPL--2.0--or--later-e0bb32?style=for-the-badge&labelColor=07101f)

<sub>Forked from <a href="https://github.com/saulob/dragons-crown-pro-pc">dragons-crown-pro-pc</a>. Runs the original PS4 x86-64 executable directly under Switch CFW using Box64 and a Switch Mesa OpenGL ES 3.2 / Core backend. <b>No game files or keys are included</b> — bring your own legally obtained decrypted PS4 Dragon's Crown Pro package.</sub>

</div>

## Overview

- **Not a Switch emulator:** executes the PS4 x86-64 game code directly via [Box64](https://github.com/ptitSeb/box64) ARM64 dynarec, custom PS4 HLE shims (kernel, threads, `audout`, saves, pad, FS), and a [shadPS4](https://github.com/shadps4-emu/shadPS4)-derived OpenGL/GLES renderer backed by [mesa-switch](https://github.com/danfromtico/mesa-switch) (Mesa 26.2.4, Nouveau).
- **Performance:** GLES 3.2 backend with persistent vertex/uniform ring buffers, lockless ImGui overlay, boosted `clkrst` clocks, async logging, and dual-mode (`NX_SAFE=0` Performance / `NX_SAFE=1` Safe) execution.
- **In-game configuration (`L3 + R3`):** click both thumbsticks together at any time to open the Switch Options menu (`nx.ini` + `dcp.ini`), adjust settings live (clocks, V-Sync, button swap, frame dumps), and save directly to the SD card.
- **FAT32 & exFAT SD support:** automatically splits archives larger than 4 GiB (`DRACRO.CPK`) into `.000`/`.001` chunks read seamlessly by the runtime.
- **Port features included:** Cheats menu, Boss Battle mode, Endgame Unlocks (`Unlock Labyrinth of Chaos`, `Unlock Ultimate + Tower of Mirages`), `Skip Intro`, Story/Tutorial Skip Offers, Unlock Gallery, Arachnophobia Safe Mode, Japanese NPC Voices, local offline PS4 trophies (`keys.json`), and built-in community localizations (English, Brazilian Portuguese, Russian, Traditional Chinese, etc.).

> **Switch limitations compared to the PC release:** Online Co-op and Discord Rich Presence are stubbed (`src/nx/nx_net_stubs.cpp`), so multiplayer is local/offline only. Must be launched in **Title Takeover** mode (hold **R** on a retail/installed game to open hbmenu with full RAM); **Applet mode** lacks enough memory.

## Supported Game Packages

You must provide your own decrypted PS4 fake `.pkg` (fPKG, image key index 3). Encrypted retail PKGs requiring per-console keys cannot be unpacked on a PC.

| Region | Title ID | Content ID | Supported Versions |
| --- | --- | --- | --- |
| **USA** | `CUSA10454` | `UP2611-CUSA10454_00-DRAGONSCROWNPRO1` | `1.00` base, or `1.00` + `1.01` update |
| **Japan** | `CUSA07001` | `JP0005-CUSA07001_00-DRAGONSCROWN0001` | `1.00` base, or `1.00` + `1.02` / `1.03` update |
| **Asia / HK** | `CUSA10209` | `HP0177-CUSA10209_00-DRAGONSCROWN0001` | `1.00` base, or `1.00` + `1.02` update |
| **Europe** | `CUSA10487` | `EP2475-CUSA10487_00-DRAGONSCROWNPRO1` | `1.03` complete base |

`scripts/prepare_game.py` verifies `param.sfo` metadata and `eboot.bin` SHA-256 hashes before accepting any package tree.

## Quick Start (Clone + `.pkg` -> `.nro` + Switch Folder)

### Prerequisites (Windows)

1. **Git** and **Python 3.10+**.
2. **[devkitPro](https://devkitpro.org/wiki/Getting_Started)** installed with the Switch toolchain (`pacman` / graphical installer):
   ```sh
   dkp-pacman -S switch-dev switch-sdl2 switch-freetype switch-libpng switch-glad switch-mesa
   ```
   *(Required for `.nro` builds; `switch-mesa` is only linked if you pass `-StockMesa`. Dependencies for Box64, Dear ImGui, tiny libraries, Python `cryptography`/`meson`, and Mesa 26.2.4 are automatically fetched and built by the scripts below.)*

### Build `.nro` and Prepare SD Folder in One Command

```powershell
git clone https://github.com/DevCrashDay/dragons-crown-pro-nx.git
cd dragons-crown-pro-nx

# Single base .pkg (creates dist-nx\switch\dragons_crown_pro.nro + dist-nx\switch\dragons_crown_pro\):
powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg C:\path\to\DragonsCrownPro.pkg
```

Common options:

- **Base + update patch `.pkg`s (or folder containing both):**
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg base.pkg,update.pkg
  ```
- **Output directly to your SD card drive (auto-splits `>4 GiB` `.CPK` files if the drive is FAT32):**
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg C:\path\to\DragonsCrownPro.pkg -Out E:\
  ```
- **Force FAT32 splitting locally (`<name>.000`, `<name>.001`) before copying:**
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg C:\path\to\DragonsCrownPro.pkg -SplitCpk
  ```
- **Build `.nro` only (without unpacking a `.pkg`):**
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\build_nx.ps1
  ```
  - By default, `build_nx.ps1` clones `ptitSeb/box64`, applies [patches/box64-switch.patch](patches/box64-switch.patch), fetches `gpu/deps.txt`, clones `danfromtico/mesa-switch`, applies [patches/mesa-switch.patch](patches/mesa-switch.patch), and builds the Mesa 26.2.4 Switch OpenGL/GLES SDK once into `third_party/mesa-switch/`.
  - Pass `-StockMesa` to link devkitPro's preinstalled `switch-mesa` instead of building Mesa 26.2.4 (note: `GL_MODE=gles` and several Mesa 26.2.4 fixes require `mesa-switch`).

### Manual Cross-Platform Steps (Linux / WSL)

`scripts/prepare_game.py` and `scripts/prepare_image.py` are pure Python (`pip install cryptography`):

```sh
# 1. Unpack & verify your .pkg(s)
python3 scripts/prepare_game.py --pkg /path/to/DragonsCrownPro.pkg --scope full

# 2. Generate boot.bin + content.bin (check folder name printed by step 1, e.g. v1.03)
python3 scripts/prepare_image.py game/prepared/v1.03 --out build/image/v1.03

# 3. Configure & build dragons_crown_pro.nro (with devkitPro env active + third_party/box64 patched)
aarch64-none-elf-cmake -S . -B build-nx -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-nx -j"$(nproc)"
```

## Installing on Nintendo Switch

Copy the generated `switch/` directory to the root of your microSD card:

```text
sdmc:/
└── switch/
    ├── dragons_crown_pro.nro
    └── dragons_crown_pro/
        ├── nx.ini              # Switch configuration (also editable in-game via L3 + R3)
        ├── game/               # boot.bin, content.bin, eboot.bin, sce_sys/, sce_module/, DRACRO*.CPK
        ├── user/               # saves, boss_battle.dat, dcp.ini, keys.json
        ├── logs/               # bringup.log, dragons_crown_pro_NNN.log
        └── cache/              # Mesa shader cache
```

1. On your Switch, **hold R** while opening any installed game/title to launch hbmenu in **Title Takeover** mode.
2. Launch **`dragons_crown_pro.nro`**.
3. First launch builds Box64 dynarec blocks and compiles initial shaders into `cache/`; subsequent boots are faster.
4. Exit cleanly anytime via **HOME -> X (Close Software)**.

## Controls & In-Game Settings Menu (`L3 + R3`)

### Switch Controls

| Switch Input | PS4 Action |
| --- | --- |
| `B` (South) / `A` (East) | Cross / Circle *(swappable live in `L3 + R3` menu via `PAD_MAP`)* |
| `Y` (West) / `X` (North) | Square / Triangle |
| `L` / `R` | L1 / R1 |
| `ZL` / `ZR` | L2 / R2 |
| `L3` / `R3` (stick clicks) | L3 / R3 |
| `+` | Options (pause / in-game menu) |
| `-` | Touchpad click |
| **Click `L3 + R3` together** | **Open in-game Switch / Options overlay** |

### Editing `.ini` Settings In-Game (`L3 + R3`)

Pressing both sticks in (`L3 + R3`) pauses game pad input and opens the **`Switch`** (`nx.ini`) tab (alongside `Display`, `Graphics`, `Gameplay`, `Controls`, and `Cheats` via `L`/`R`). Every change writes `sdmc:/switch/dragons_crown_pro/nx.ini` immediately:

| Key (`nx.ini`) | Values | When Applied | Notes |
| --- | --- | --- | --- |
| `VSYNC` | `0` (`Off`, default) / `1` (`On`) | **Live** | `0` avoids frame-pacing sawtooth stalls against Horizon NWindow |
| `NX_BOOST` | `1` (`Boosted`, default) / `0` (`Stock`) | **Live** | Switches CPU (1785/1020 MHz), GPU (460/307 handheld, 768 MHz docked), EMC clocks via `clkrst` |
| `PAD_MAP` | `""` (`Default`), `cross=b,circle=a`, `cross=b,circle=a,square=y,triangle=x` | **Live** | Remaps face buttons on the fly (`nx.ini` supports custom mappings too) |
| `FRAME_DUMP` | `0` (`Off`), `300`, `600` | **Live** | Writes BMP screenshots to `logs/` every N frames |
| `NX_SAFE` | `0` (`Performance`, generated default) / `1` (`Safe`) | Restart | `0` enables guest-hook features (skip intro/offers, boss battle, endgame unlocks, cheats overlay); defaults to `1` if omitted from `nx.ini` |
| `GL_MODE` | `gles` (`GLES 3.2`) / `core` (`Desktop GL`) | Restart | `gles` + `NX_PERSISTENT_RING=1` gives the fastest path with `mesa-switch` |
| `AUDIO` | `sdl` (`On`) / `none` (`Off`) | Restart | Direct libnx `audout` (48 kHz stereo S16) |
| `NX_SCHED` | `spread` (default) / `mapped` / `off` | Restart | Guest thread core/priority scheduling |
| `NX_GPU_THREAD` | `""` (`48:2` default) / `44:2` / `59:2` / `off` | Restart | Priority/core pinning for `GpuCommandProcessor` |
| `NX_PERSISTENT_RING` | `1` (`On`, default) / `0` (`Off`) | Restart | Persistent `GL_EXT_buffer_storage` rings |
| `STALL_DUMP` / `GL_DEBUG` | `0` (`Off`) / `1` (`On`) | Restart | Diagnostic bridge stall / `KHR_debug` logs |

## Technical Notes & Testing

- **Ryujinx automation:** `tools\ryu_run.ps1` deploys a built NRO and test `nx.ini` to Ryujinx SD (`-Ini`), injects scripted controller sequences (`-Pad "42:l3+r3,45:down,47:right"`), and collects timestamped `runs/<stamp>-<label>/` logs and frame dumps.
- **Technical handoff & architecture notes:** see [HANDOFF_NX.md](HANDOFF_NX.md) and upstream docs in [docs/](docs).
- **Reporting issues:** attach both `sdmc:/switch/dragons_crown_pro/logs/bringup.log` and the newest `dragons_crown_pro_NNN.log`, along with whether you launched in **Title Takeover** or **Applet mode**, docked/handheld, and your `nx.ini`.

## Credits

- **Dragon's Crown Pro** by Vanillaware / Atlus (2018). Unofficial fan port; contains no copyrighted game files or keys.
- **[dragons-crown-pro-pc](https://github.com/saulob/dragons-crown-pro-pc)** by saulob for the original PS4-to-native port, HLE runtime, build verification, and localization work.
- **[Box64](https://github.com/ptitSeb/box64)** (MIT) by ptitSeb for the x86-64 to ARM64 dynarec/interpreter core adapted in `src/nx/` and [patches/box64-switch.patch](patches/box64-switch.patch).
- **[mesa-switch](https://github.com/danfromtico/mesa-switch)** for Mesa 26.2.4 Nouveau/Horizon OpenGL + GLES 3.2 support, with Horizon VA fixes in [patches/mesa-switch.patch](patches/mesa-switch.patch).
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** (GPL-2.0-or-later) for the GPU command processor, shader recompiler, video core, and pre-removal fPKG extractor design.
- **[devkitPro](https://devkitpro.org)** (`devkitA64`, `libnx`, Switch `portlibs`).
- **[TRG Launcher](https://github.com/TekRantGaming/trg-launcher)** (`settings.cpp`, MIT), **[Dear ImGui](https://github.com/ocornut/imgui)**, **[Zydis](https://github.com/zyantific/zydis)**, **[xxHash](https://github.com/Cyan4973/xxHash)**, **[fmt](https://github.com/fmtlib/fmt)**, **[miniz](https://github.com/richgel999/miniz)**, and localization contributors credited in [localization/ru/README.md](localization/ru/README.md).

## AI Disclosure

AI tools were used to assist with porting, Switch optimization, debugging, tooling, and documentation. Changes were tested on Ryujinx and real Nintendo Switch hardware.

## License

Original code in this repository is licensed under the GNU General Public License v2.0 or later ([LICENSE](LICENSE)). Third-party dependencies retain their respective licenses ([packaging/licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt)). No game data or Sony/Nintendo proprietary keys are distributed.
