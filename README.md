# Metroid Prime 4 Recomp — native Windows build

[![Listed on Decomp Buddy](https://decompbuddy.com/badge.svg)](https://decompbuddy.com/games/metroid-prime-4-beyond-metroid-prime-4-recomp-nintendo-switch)

A native PC build of **your own copy** of *Metroid Prime 4: Beyond* for Windows. It is built on the SwitchRecomp runtime
by MZ Labs: the game's Switch code is recompiled ahead of time into a native Windows DLL on your PC, and the Switch GPU's
work is translated to OpenGL.

This is a Windows build of [linuxopt/mp4-recomp](https://github.com/linuxopt/mp4-recomp), which develops and play-tests
the game on Linux. The game code, renderer and audio are the same; see that project's README for the full list of
rendering, stutter, shader and audio fixes.

> **No Nintendo files are included.** No game code, no RomFS, no keys, no generated C. You need your own legally
> obtained, dumped copy of the game. The release holds the runtime, the recompiler and a bundled GCC and Python; it
> compiles the game from your files during the first-run setup. The only game-related file inside is a manifest of
> RomFS file names, sizes and SHA-1 hashes, used to verify your dump.

## Requirements

- **Windows 10 or 11, 64-bit.**
- **GPU:** an OpenGL 4.3+ driver (AMD, NVIDIA or Intel).
- **Your game dump:** *Metroid Prime 4: Beyond* for Switch, program `010019A01E2F2000`, with the update whose main has build
  ID `8275FBE1EC5191C279F3A943AD8524F4BAB087A3` (the cartridge alone is build `E860CBED…` and is refused). Either:
  - the **files** from your Switch: the game as `.xci` (cartridge) or `.nsp` (eShop) and the update as `.nsp` (DBI or
    nxdumptool), plus your Switch's **`prod.keys`** (Lockpick_RCM) in `%USERPROFILE%\.switch\` or
    `%APPDATA%\Ryujinx\system\` (yuzu-style `%APPDATA%\<emulator>\keys\` folders work too); or
  - an **extracted** ExeFS (`main`, `main.npdm`, `rtld`, `sdk`, `subsdk*`) and RomFS (`Worlds\`, `UI\`, `Preload\`,
    `Kernel.pak`, …) with the update applied.

  Setup checks the build ID and verifies all 445 RomFS files (26.9 GB) against the manifest.
- **About 30 GB of free space** on the drive you put the game folder on.

Controllers, audio and the game's videos need nothing extra: SDL2 is included and the video decoders are built in.

## Install and play

1. Download `MetroidPrime4Recomp-PC-vX.Y.Z.zip` from the [latest release](../../releases/latest) and extract it
   anywhere with room, for example `D:\Games\`. You get a `Metroid Prime 4 Beyond` folder.
2. Run **`Metroid Prime 4 Beyond.exe`** in it.
3. **First run:** the setup screen asks for your game (its `.xci` / `.nsp` file, or an extracted folder) and the update
   (its `.nsp`, or an extracted folder). Press **Start Setup**: it verifies the files, copies them into the game folder
   and compiles the game with the bundled GCC (a few minutes on a fast CPU).
4. Press **Play**. The first launch builds shaders in the background; later launches load them from the cache.

Everything stays in the game folder, next to the EXE:

| Path | What |
|---|---|
| `Metroid Prime 4 Beyond.ini` | settings, rewritten with every option explained on the first start |
| `Metroid Prime 4 Beyond_data\` | the installed game data, saves, cheats and mods |
| `shader_cache\` | compiled shaders |
| `Metroid Prime 4 Beyond.log`, `setup.log` | logs for bug reports |

The Play screen can also add the game to Steam as a non-Steam game and install updates from this repository's releases.

## Saves

Saves are plain files in `Metroid Prime 4 Beyond_data\save\user\`.

**Using your Switch save:** export it with a save manager on a homebrew-enabled Switch (JKSV or Checkpoint), close the
game, back up the existing `save.bin`, and copy the exported files into that folder. A save from a newer game update
than the one this build targets may not load; restore your backup if it doesn't. Nintendo's cloud saves can't be
downloaded to a PC.

## Controls and the in-game menu

- Xbox controllers work as they are; SDL2 adds Switch Pro, Joy-Con, DualShock 4, DualSense and 8BitDo over USB or
  Bluetooth (pair Bluetooth controllers in Windows Settings first). **ZL is the left trigger.**
- Keyboard and mouse play like a PC shooter: **WASD** to move, **mouse** to look, **left click** shoots, **right click**
  locks on, **middle click** or **F** fires the subweapon, **Space** jumps, **E** interacts, **Left Ctrl** or **C** is the
  Morph Ball, **Q** switches visors, **V** is the Vi-O-La, **1-4** pick a shot, **Esc** or **Tab** opens the game's menu.
  **G** fires the lasso (keep holding to yank up). In the Morph Ball: left click bombs, **F** power bombs, **Space** boosts
  (hold, release), **Q** springs, right click is the Spider Ball. On the Vi-O-La: left click goes, **Shift** or right click
  brakes, **Space** boosts, **E** fires.
- **F1** or the Home / Guide button opens the in-game menu (settings, cheats, FOV, upscaling).
- **F3** saves a screenshot to `captures\`, **F4** toggles the 60 fps limit, **F5** writes a frame trace for bug
  reports, **F11** toggles fullscreen.

## Known issues

- **Windows is newer than Linux here.** The game was play-tested on Linux; the Windows build compiles from the same code
  and reaches the setup screen, but has had less play time. Please report what you find.
- **Shader compile threads** with their own OpenGL contexts are Linux-only so far, so new areas may stutter a little more
  while shaders compile.
- **VSync is off by default** on Windows (`forced_vsync=1` in the `.ini` turns it on).
- **First visit to a new area** can still compile a handful of shaders the game loads moments before drawing them.
- **Loading a save** freezes for about 0.5 s while the lighting volumes upload.
- **The FOV slider** has no effect yet (the camera address for this build hasn't been found).
- **Not modelled yet:** a filter effect on two audio submixes and an aux effect, so a few sounds may be clearer than
  intended.

## Troubleshooting

- Attach `Metroid Prime 4 Beyond.log` (the last five are kept) and `setup.log` to bug reports. The Play screen's bug report
  button zips them for you.
- Screenshots with F3; a frame trace with F5 at the moment something looks wrong helps most.
- Setup can't find `prod.keys`: put it in `%USERPROFILE%\.switch\`, or choose an extracted game folder instead.
- Rendering glitches: delete the `shader_cache` folder once to rule out a stale cache (the next launch rebuilds it).

## Building from source

Developer notes and the bring-up log are in [DEVELOPMENT.md](DEVELOPMENT.md). On Windows, from a PowerShell prompt in
the repository:

```powershell
powershell -ExecutionPolicy Bypass -File windows\get_tools.ps1
$env:GLSLANG = "$PWD\third_party\glslang\bin\glslang.exe"
.\build.bat release 1.2.3
```

`get_tools.ps1` downloads the pinned toolchain (GCC 14.2.0 from winlibs, Python 3.12 embeddable, glslang 16.6.0, SDL2)
and checks each file's SHA-256. `build.bat release X.Y.Z` puts the game folder and its zip in `release\X.Y.Z\`; plain
`build.bat` builds only `bin\runtime_win64_win32.exe`.

**Releases** are built by GitHub Actions ([.github/workflows/release.yml](.github/workflows/release.yml)): push a tag
`vX.Y.Z` and the workflow builds on Windows and publishes the zip, the in-place update package and `update.txt` as a
GitHub release.

## Credits

Copies, forks and builds of this project must keep these credits where users can see them (NOTICE.txt).

### Source

This repository is a Windows build of **[linuxopt/mp4-recomp](https://github.com/linuxopt/mp4-recomp)**, taken at
commit [`31ff5d5`](https://github.com/linuxopt/mp4-recomp/commit/31ff5d561df3058c0b45a09fa0d4553fdc4a7795)
(release `v2026.11.02`) with its full history. All the game work (the Prime 4 bring-up, rendering, shader, audio and
video fixes, the setup and Play screens) is theirs. linuxopt/mp4-recomp is in turn a fork of
[Metroid Dread Recomp](https://github.com/DaniSald/Metroid-Dread-Recomp) by yozander2009-stack and contributors.

### Windows port

The Windows build, packaging and releases in this repository were made by **PlagueDrRiess** with **Claude**
(Claude Opus 5.5, by [Anthropic](https://www.anthropic.com)) as AI pair programmer, who co-authored these commits:

- the release packager fix that let `build.bat release` finish (`beyond/make_release.py`),
- `windows/get_tools.ps1` (the pinned, checksum-verified toolchain) and the GitHub Actions release workflow,
- this Windows README, the Windows text in the setup screen and the release's `README.txt`, and pointing the Play
  screen's updates and bug reports at this repository.

### Everyone else

- **yozander2009-stack** and contributors: the recompiled runtime and launcher this port is built from
- **linuxopt**: the Metroid Prime 4 fork and its fixes (made with Claude as AI pair programmer too), including the later
  Ice Belt fog and Control Beam fixes and the built-in cheat hooks ported here from
  [`079dfb0`](https://github.com/linuxopt/mp4-recomp/commit/079dfb0)
- **Gamerjin**, **Luffysan** and others on GBAtemp, with pointers by **ZiT866**: the cheats behind the built-in cheat hooks
- **melos444** (GameBanana): the mod the Green Energy 10x option is based on
- **MZ Labs**: SwitchRecomp (Apache-2.0); "SwitchRecomp by MZ Labs" and NOTICE.txt must be kept
- **ScarletNX**: the native Vulkan renderer (GPL-3.0)
- **AMD**: FidelityFX Super Resolution 1 (MIT)
- **Dear ImGui** (MIT), libvpx and OpenH264 (BSD), SDL2 and zlib (zlib licence), GCC / MinGW-w64, Python
- Program icon: [Metroid icon created by Smashicons - Flaticon](https://www.flaticon.com/free-icons/metroid)

## Licences

The released program is distributed under **GPL-3.0** (`LICENSE`), because it contains the ScarletNX renderer; its
source is this repository. Each source file keeps its own licence (`LICENSE.txt` and `NOTICE.txt` for SwitchRecomp's
Apache-2.0; Apache-2.0 and MIT code is GPL-3.0-compatible). The licence texts are also in the release's `licenses`
folder.

*Metroid Prime 4: Beyond* is © Nintendo. This project is not affiliated with or endorsed by Nintendo.
