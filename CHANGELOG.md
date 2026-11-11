# Changelog

Fixes and changes per release. Found a problem? Please use the
[bug report form](../../issues/new?template=bug_report.yml) and attach `Metroid Prime 4 Beyond.log`: it holds the
timings and errors needed to trace most issues, and one report per problem keeps each one easy to track.

## 2026.11.18

### Better problem reports (thanks to IP050)
- **REPORT** (About page) now packs all five logs instead of two, since after a restart the crashed run is no longer
  the newest one, plus Native Vulkan's note of what the driver was building.
- When a log holds a crash, the zip gets a `crash.txt` first: the crash itself with the lines before it, without the
  per-call chatter of a detailed log.
- Your Windows user name is taken out of every path in the files packed.
- When your last run crashed, the launcher tells you once and points at REPORT.
- Nothing is sent anywhere: as before, the zip is made in the game folder and the bug form opens for you to attach it.
- IP050's standalone packer, `tools/MP4-Report.ps1`, is in the repository for anyone who wants the Windows Event
  Viewer entry as well. It is not part of the download.

## 2026.11.17

### Ultrawide: Picture shape (on trial)
New setting under **Display > Picture shape** in the launcher and the in-game menu: 16:9 (the game's own), 21:9,
32:9, 16:10, or **Fill the window**. Wider shapes show more of the world to the sides instead of black bars. It
applies while playing. The HUD and menus are stretched along with the picture for now; keeping them at their own
shape is the next step. For the v1.1.0 update of the game only: other versions stay 16:9.

## 2026.11.16

### Native Vulkan is switched off for now
2026.11.15 fixed its crash after the opening movie, but the world then renders black with it, on NVIDIA and AMD
alike: the game's lighting volume is not drawn right yet. Until that is fixed the launcher no longer offers Native
Vulkan, and a saved choice of it becomes **Vulkan output**, which draws the game correctly. OpenGL is unchanged.

### Title screen music on both sides
The title music played on one speaker only, with pops on the other, while sound effects were fine. One of the
music's two halves was being emptied after every piece of sound it was given. Fixed, on every renderer.

### Launcher
- A new look for the launcher.
- **Save in use**: other saves to play from, kept apart from your own (for example a fresh game without touching
  your progress).

### Antivirus warnings
Windows Defender flagged the 2026.11.15 download for some people (`Trojan:Script/Sabsik.EN.A!ml`, a machine-learning
guess about a new file, not a match with known malware). [SECURITY.md](SECURITY.md) now explains what that means and
how to check a release yourself: the checksum, the public build, and a VirusTotal lookup.

### If the game crashes at start on an AMD card (OpenGL or Vulkan output)
One report shows the crash inside AMD's OpenGL driver while it compiles shaders at startup. Not fixed yet: please try
`shader_threads=-1` in `Metroid Prime 4 Beyond.ini`, then `prebuild_shaders=0` as well if it still crashes, and
report which one gets you in.

## 2026.11.15

### Native Vulkan: the crash after the opening movie of a new game
Starting a new game on Native Vulkan ended the game right after the opening movie, at the first 3D scene, on AMD
and on NVIDIA cards alike. Three of the shaders the game uses there were translated with an instruction that is not
valid, and the graphics drivers crashed building them. The translation is fixed. This was reproduced and fixed on
NVIDIA; if you had this crash on AMD, please report whether it is gone. The first start builds its shaders again.

### Languages
- Brazilian Portuguese is now a language choice ("Portuguese (Brazil), on trial").
- New "Follow system" choice, the default for a new installation: the game uses your PC's display language, and
  English when the game has no match. An existing installation keeps its language until you pick "Follow system".
  If the game's text is missing in your language, choose English and please report it.

## 2026.11.14

### AMD Radeon, Native Vulkan: a possible fix for the cutscene crash, and a way to find it if not
- The threads that build shaders now have 16 times the stack space (16 MiB, was 1 MiB). The game's larger shaders may
  have run AMD's shader compiler out of stack, which ends the game without a crash report. This is not confirmed as
  the cause: if you had this crash, please report whether it is gone.
- Before the graphics driver builds a pipeline or shader, the game now notes which one in
  `vulkan_native\building.txt`. If the game ends there, the next start writes that note into the log
  ("the last run ended while the driver was making this"). If it still crashes for you, start the game once more
  and attach the log from that second start.

## 2026.11.13

### Native Vulkan starts again after a crash
On some AMD cards (RX 9070 XT, Ryzen Z1 / Legion Go) Native Vulkan crashes during the opening cutscene, and after
that the game stayed on "Loading" at every start until the `vulkan_native` folder was deleted by hand. After a run
that did not close properly, Native Vulkan now sets its shader cache aside (as `vulkan_native\shader_cache.crashed`)
and builds a new one, so the game opens again by itself. The first start after a crash builds shaders again.

The cutscene crash itself is not fixed yet. To find it:
- The game now keeps its own crash reporter in place when a driver installs another one over it, and the log names
  the file that did.
- A program exit asked for by a driver is now written to the log too.

If the game crashes for you, please attach the log from this version.

## 2026.11.12

### The game stays at console speed
Metroid Prime 4 moves forward one step per frame, so the 120, 144 and 240 FPS choices ran the whole game faster
instead of smoother (2.4 times the speed at 144 on Native Vulkan; OpenGL stayed at 60 regardless). The game now
runs at 60 on every renderer, a saved 120/144/240 is set back to 60 once, and "Best settings for this PC" picks 60.
Unlimited is still there and says that it speeds the game up. Smooth output above 60 for high-refresh screens is
planned as frame generation.

### AMD Radeon crashes: a workaround to try and clearer reports
Some AMD cards (RX 9070 XT, Ryzen Z1 / Legion Go) crash in AMD's driver about 2 seconds after PLAY on OpenGL, or
during the opening cutscene on Native Vulkan. This release does not fix that yet, but:
- `shader_threads=-1` in `Metroid Prime 4 Beyond.ini` makes every shader on the main graphics thread. On OpenGL,
  try it if the game crashes right after starting.
- A crash inside a driver now names the thread and the exact game code that called the driver, and crashes that
  end the game at once now still reach the log. Please attach the log from this version to your report.

### Checksums for every release
Each release now has `SHA256SUMS.txt` to check downloads against (see SECURITY.md). SECURITY.md also explains how
to report a security problem privately, and NOTICE.txt now requires keeping the contributors' credits in copies.

### Updates install themselves
When the launcher finds a new version it now downloads and installs it straight away, then restarts or rebuilds the
game code if the update needs it, with no UPDATE press. PLAY waits while it downloads. Every file is still checked
before anything is replaced, and UNDO still goes back a version (that version is then not installed again by
itself). It can be turned off in the Play screen's Updates section. Players on older versions press UPDATE once more
to get this.

### Progress bar on the splash screen
The splash shown after PLAY now has a progress bar and a label ("Preparing shaders", then "Starting the game"). Most
of the wait is preparing the shaders cached from earlier sessions, and the bar follows that real progress.

## 2026.11.11

### Scan visor no longer drops to 2 FPS at higher resolutions
With the resolution setting above or below 1 (for example 2 for 4K), the scan visor ran at about 2 FPS whenever it
was open. To find what the reticle is over, the game reads a small patch of depth and stencil around it each frame;
before that read the whole depth buffer was scaled down to the game's size, which took 600-770 ms per read on NVIDIA.
The patch is now read directly from the full-resolution buffer. Measured at resolution=2: 1-12 ms per read and
55-60 FPS while scanning (was 1.5 FPS). Scans still lock on and complete. Resolution 1 was not affected.

### Setup reads more .xci / .nsp files
- **Files and folders with non-English names.** A game file whose name or folder path had characters outside the
  Windows language setting (Japanese, Cyrillic, Arabic and so on) failed with "Cannot open". These paths now work.
- **Full cartridge dumps** that keep the card's key area in front of the header are now recognised.
- **Cartridges that hold the game and an update together** now use the base game, and choosing an update file as
  the game gives a clear message instead of a failed unpack.

## 2026.11.10

- Docked mode is the default (1920x1080 output instead of handheld 1280x720), and the game no longer waits for
  shaders by default. Existing settings get both once.
- The splash screen closes at the first picture on every renderer (it could stay up on Native Vulkan).
