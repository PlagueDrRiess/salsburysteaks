# Changelog

Fixes and changes per release. Found a problem? Please use the
[bug report form](../../issues/new?template=bug_report.yml) and attach `Metroid Prime 4 Beyond.log`: it holds the
timings and errors needed to trace most issues, and one report per problem keeps each one easy to track.

## 2026.11.31

### Native mouse aim (issue 28)
The mouse now turns the camera itself instead of acting as the right stick. Your movement goes straight into the
game camera's own turn and pitch every frame: no stick curve, no acceleration ramp and no top turning speed, so the view
goes exactly as far and as fast as your hand. A controller's stick still works alongside it.
- **Lock-on:** while the lock button is held (right click by default), the mouse moves the free-aim reticle
  directly, at the same speed as the camera, so the feel and your DPI carry over. The reticle resets on release, as
  in the game.
- **Sensitivity:** `mouse_sensitivity` 100 is 0.04 degrees a mouse count, a full turn in about 29 cm at 800 DPI.
- **Pitch** stops at the game's own limits.
- **Default:** "Camera, 1:1 (raw mouse)" is the new default in Controls (`mouse_mode=raw`). If you had the old
  right-stick mode, it switches to raw once; you can pick "Right stick" or "Motion" again any time.
- The update rebuilds the game code once (a few minutes), since the game code needs three small additions for this.

### Mouse clicks work in the menus
On the title screen, file select, pause menu and loading screens, left click is A (confirm) and right click is B
(back), so you no longer need E or Enter there. In play the buttons keep their bindings (shoot, lock on).

### Cheats no longer slow the game down over time (issue 30)
Cheat codes that work on the player's stats look for their table in the game's memory. When it couldn't be found, the
search went through the whole 6 GB of game memory again every minute for as long as the game ran, which on long
sessions turned into stutter. It now stops after three tries and says so in the log, and looks again when you change
cheats or after 10 minutes. Note too: `fps_limit=0` (unlimited) makes this game run faster and unevenly, so keep 60.

## 2026.11.30

### Smoother play: the GPU thread no longer stops for the game's GPU counters
The game counts things on the GPU (the scan visor's progress, and many effects in the intro and in fights) and reads
the counts back. Each read stopped the rendering thread until the GPU had finished everything queued, 20-110 ms at a
time, several times a second and before draws. The counts now come back in the background. Only the "done" signals the
game waits for are held until they arrive, as on the Switch, so scans still finish. In a recorded play session the
share of frames over 20 ms went from 20% to under 9%. `SWR_GW_SYNC_OLD=1` brings the old way back for comparison.

### No hitch when an area unloads its textures
Deleting a texture the GPU might still be drawing with made the driver wait for the GPU: 30-190 ms each, and an area
unload frees hundreds. Freed textures are now deleted 3 frames later, a few per frame. They also free two views that
were never freed before (driver memory that grew with every area). `SWR_TEX_DELETE_NOW=1`: the old way.

## 2026.11.29

### Shaders are prepared before you play (NVIDIA, OpenGL and Vulkan output)
The game now waits on its own logo until the shaders it knows of are built, instead of building them while you play
(the cause of the held pictures, ghosting and cutscene desync in issues 8 and 18). A thin bar under the logo shows the
progress, and **Esc** goes straight on.
- The wait uses every spare CPU thread, then goes back to the usual three helpers for play.
- The draw states NVIDIA's driver compiles at first use are drawn once ahead of time too, so the driver keeps them.
- **The first start from an empty shader cache takes long:** about 30 minutes on a 12-thread CPU (it goes on after
  30 minutes at most). Later starts find everything built and go straight in. Keeping the `shader_cache` folder when
  you update keeps that work.

### Fewer hitches from textures
A compressed texture's upload asked the graphics driver a question that made it wait for all of the frame's queued
work, 13-36 ms each time a texture loaded. It doesn't ask any more. In a recorded play session the worst frame went
from 251 ms to 93 ms.

### A way to quit (issue 27)
- **QUIT** next to PLAY on the Play screen.
- The controls list says where it is in the game: F1 (or Home) opens the menu, QUIT GAME is at its bottom. Esc opens
  the game's own pause menu, which has no quit, like on the Switch.
- With Native Vulkan, where that menu can't show yet, F1 asks "Quit the game now?" instead.

### Radeon RX cards: the very first start too (issues 1, 11, 16, 26)
2026.11.28 moves Radeon RX cards to Native Vulkan, but only once the settings file had a renderer line, so the very
first start still began on OpenGL and crashed. It now applies from the first start, and Radeon PRO cards (the same
driver) are included.

## 2026.11.28

### Radeon RX cards start the game (issues 1, 11, 16, 26)
On Radeon RX cards (RX 7800 XT and RX 9070 XT in the reports) AMD's OpenGL driver crashes a few seconds after boot,
on a thread of its own, with OpenGL and with Vulkan output. The same card plays fine with Native Vulkan, so the
game now uses Native Vulkan on Radeon RX cards automatically, and the log says so. Integrated Radeon graphics (Legion
Go and other handhelds and laptops) stay on OpenGL, which works there. `SWR_AMD_OPENGL=1` keeps OpenGL. Please report
whether it starts for you.

## 2026.11.27

### Shaders from a real playthrough, built before you meet them
The game now notes every shader variant it draws, and this release ships the list from a playthrough (about 1,500
variants: IDs and a few table values, no game code). At each start the game builds the ones it can from your own
game files in the background, before the areas that use them, so fewer objects are missing or ghosting the first
time you see them. The list grows with later releases.

### Controller rumble really works now (issue 22)
The game sends rumble for every controller slot at once (handheld, full controller, both Joy-Con sides). The empty
slots overwrote the real rumble straight away, so nothing reached the controller. Each slot is kept apart now and
the controller gets the strongest.

### No freeze at start with a misbehaving controller driver
The launcher and the game asked Windows' DirectInput for the name of every input device; one that never answered
(a broken virtual gamepad driver) froze them before the window appeared. Controllers are now found through XInput
and SDL's own drivers, which cover Xbox, PlayStation and Switch pads. `SWR_DIRECTINPUT=1` brings DirectInput back
for older generic pads.

## 2026.11.26

### Controller rumble (issue 22)
- DualShock 4 and DualSense controllers rumble now. SDL only sends rumble to them in its enhanced mode, which was
  off, so they stayed still (most of all over Bluetooth).
- A controller that won't take rumble through SDL (some third-party pads that present themselves as a Switch Pro
  Controller) gets it through XInput as well, if it also shows up there. The log names a controller that refuses.
- The log now records the game's first real rumble and the first one sent to the controller, so a report shows
  where it stops.

## 2026.11.25

### Objects missing or ghosting for seconds on a fresh install (issues 8 and 18)
On the first starts the game builds about 28,000 shaders from your game files in the background. A shader the game
needed right away was written to disk behind all of them, so the helpers got it late: logs showed waits of 8, 20, 48
and even 167 seconds, with objects missing or ghosting all that time. A shader the game is waiting for now goes ahead
of everything else, and the background build pauses while the disk writes are behind.

### "Wait for new shaders" no longer turns off building ahead
With **Wait for new shaders** on (`wait_for_shaders=1`), the helpers and the background build were switched off too,
so every shader was built at its first draw while the game waited: freezes of a second or two each, 23 seconds in one
cutscene. The setting now only makes draws wait; shaders are still built ahead.

## 2026.11.24

### Linux: builds again, with keyboard and mouse bindings and mouse aim (thanks, patrickksmdev)
- The Linux build links again, and VP9 cutscenes use the system's libvpx when the bundled one isn't there.
- Keyboard and mouse can be rebound in the menu and the `bind_` lines of the ini, as on Windows; mouse aim works in
  both Stick and Pointer mode, and the lasso key too. Esc opens the game's menu instead of quitting.
- On Mesa drivers with less than 24 GB of memory the shader pre-build is off for now: it kept every shader program in
  memory until a 16 GB system ran out and the game crashed. The log says when this applies. Everywhere else it stays
  on.

### Portuguese (Brazil) in the language list (thanks, patrickksmdev)
The console's language list now has all 18 languages for every way the game can ask for it, Portuguese (Brazil)
included.

## 2026.11.23

### Freezes after clearing the shader cache
Clearing the `shader_cache` folder but keeping the `seed_*` files made the game think every shader of the seed was
already built. It skipped building them in the background, and each one was built at its first draw instead: freezes
of a second or more in play and cutscenes, however long the game was left running. The game now checks the built
shaders on disk at start, and if most of them are gone it builds the whole seed again in the background (the log says
"the shader cache was cleared"). To start clean, deleting the whole `shader_cache` folder is fine.

## 2026.11.22

### Ultrawide works (issue 17)
21:9 and 32:9 showed the 16:9 picture stretched: the game's aspect ratio sits in read-only memory and the write was
refused. It is now patched directly, and the picture really is wider. If it can't be, the log says so.

### The arm cannon looks like the game's again
The default view is back: the cannon with its upper-arm armor, and Samus's left hand at the bottom. The enlarged arm
from earlier builds put the barrel out of line with the aim; it is now off by default (`arm_cannon_large=1` under
`[Display]` turns it back on).

### Smoother play and faster loading
- The game no longer loads every saved shader at start-up, which slowed the first minutes: frames over 20 ms dropped
  from 20.6% to 11.7% in the same test.
- The shader cache is compacted: it loads in about 7 seconds instead of 35, and takes 1.3 GB instead of 6.9.
- Shaders that use jump tables are now built ahead too, like the rest.
- The game's GPU timer runs smoothly, so its dynamic resolution stays at its sharpest size more often.
- If you set `wait_for_shaders=1` in the ini, set it back to `0`: with it on, every new shader is built while the game
  waits, which shows as freezes of up to 2 seconds each.

### Field of view
- New **Edge look-ahead** setting under **Display** (`fov_lookahead`, default 40%): the game keeps the parts of the
  level just past the screen edges, so no white or empty areas appear at the sides at wide settings.
- The arm, aim and lock-on keep matching the picture at any field of view.

### Controls
- Keyboard and mouse can be rebound in the menu.
- Mouse **Pointer** mode: looking up and down follows the mouse the right way, the mouse button fires, and turning is
  finer.
- Escape no longer closes the menu right after opening it.

### Other
- AMD cards decode the game's textures on the CPU, to avoid a crash in the AMD driver (issue 16). Please report if it
  still happens.
- The last three play sessions' logs are kept (`.lastplay.log`, `.lastplay2.log`, `.lastplay3.log`).
- The Dread button-icon pack download is removed.

## 2026.11.21 (fix for 2026.11.20)

### Setup stopped at "Applying Metroid Prime 4 Beyond fixes"
2026.11.20's new field-of-view and shader hooks change the generated game code, and setup's check of that code
(before it applies the Metroid fixes) still expected the old code, so it stopped with "The generated code does not
match the verified RS15 build (code_110.c)". Deleting the build folder did not help. The check now expects the new
code: update, and setup goes on from where it stopped.

### Cutscenes: no more cream-coloured shapes or ghost images, and the sound stays with the picture (issue 8)
- A frame where some objects could not be drawn yet (their shaders still building) is no longer shown: the last whole
  picture stays up for a moment instead. Those frames showed Samus and the marines as flat cream shapes and left
  ghost images piling up. `SWR_NO_FRAME_HOLD=1` shows them as before.
- After a hitch the sound now waits for the picture to catch up, instead of staying ahead for the rest of the
  cutscene. `SWR_NO_AV_HOLD=1` turns this off.

## 2026.11.20 (test release)

### Every shader of the game, built ahead (like the Tears of the Kingdom recomp)
The game keeps its shaders in its own archives. The first start now reads all 67,430 of them out of your game files
(about 13 seconds, in the background) into `shader_cache\seed.bin`, translates the ones that can be done ahead on a
spare CPU core (about 2 minutes, once), and builds them in the background while you play. Later starts reuse all of
it. On the test PC the game held 60 FPS while this ran, and draws that had to wait for a new shader dropped from
about 470 to 170 on the second start. Shaders that use jump tables are still built when first drawn; that is next.
Set `SWR_NO_SEED=1` to turn it off if it causes trouble, and please report it.

### Field of view that works
- The **Field of view** setting (now under **Display**) widens the game's camera itself.
- The visor, HUD, minimap and the red hit overlay keep their size and place at any field of view.
- Things at the edges of a wide view are no longer hidden (grey sky at the sides of the tram tunnel at 160%), and the
  game sees a little past the edges so it can load what is about to come into view.
- Known: at very wide settings some distant objects (far doors) can still disappear early. Being worked on.

### Textures appear sharp straight away
AMD and NVIDIA desktop cards cannot read the game's compressed textures (ASTC). They were decoded on the CPU in the
background, which could take up to 1.4 s after a texture was on screen. They are now decoded on the graphics card
the moment they load. (`SWR_ASTC_CPU=1` returns to the old way.)

### AMD Radeon: a third attempt at the crash at start (OpenGL and Vulkan output)
The crash is inside AMD's own OpenGL driver, on a shader-compile thread the driver starts by itself. AMD cards now
build every shader on the one OpenGL thread, with neither our compile threads nor the driver's (`shader_threads=2`
or more in the ini brings them back). Not confirmed yet: no AMD card is available to the project, so please report
whether the game now starts.

### Sound when no audio device was ready at start
If the sound device was not ready when the game first played audio (a headset or Bluetooth speaker still
connecting), the whole run was silent. The game now tries again every 2 seconds, and reconnects if the device goes
away while playing.

### Mouse look (issue 10)
Mouse movement is no longer lost between frames, the view stops where the mouse stops (no overshoot), sensitivity
goes up to 1000%, and the **Motion** mode is in the launcher and the in-game menu.

### Menus
- The in-game menu (F1) has a visor reticle as its pointer, and the launcher uses it too.
- The in-game menu writes to the log why it closed, to track down a report of it closing by itself.

### Known issues in this test release
- **AMD Radeon crash at start** (issues 11, 1): the third fix is in, but untested on AMD hardware. Reports welcome.
- **Native Vulkan** (issues 1, 13): the view can turn white in some areas, scan marks may not show (which can block
  progress after the first cutscene), a glitch in the first seconds of the map-upload cutscene, and FPS drops. Play
  on OpenGL or Vulkan output for now; Native Vulkan gets its own work session.
- **Field of view above about 120%**: some distant objects (far doors) disappear earlier than they should, because
  the game decides their detail from their size on screen. Being traced.
- **Shader stutter on first visits**: shaders that use jump tables (about half) are still built when first drawn.
- **Ultrawide** (issue 9): the world widens, but the HUD and menus are stretched.
- **Save station on load**: its base can show black for a moment, then light up.
- **Frame pacing**: in heavy scenes frames can alternate fast and slow while averaging 60 FPS.

### Native Vulkan: F8 and F5 captures
F8 and F5 now work on Native Vulkan too: the next frame is written to `captures\vk\NNN` with a line for every draw
and pictures of the render targets (F8: per pass; F5: after every draw). They are for tracking down picture faults.

## 2026.11.19

### AMD Radeon: the crash at start on Vulkan output
On AMD cards (RX 9070 XT, RX 7800 XT, drivers 26.10.1 and 26.10.2) the game crashed about two seconds after Play
with "invalid memory access" when the renderer was Vulkan output. The crash is inside AMD's OpenGL driver, in the
call that hands the first frame to Vulkan. On AMD cards Vulkan output now puts the picture out through OpenGL
instead, so the game starts. Thanks to everyone who sent a report zip: its `crash.txt` showed this directly.
If it still crashes for you on **OpenGL**, please send a new report.

### Native Vulkan is back, and its world is lit
The black world is fixed: the game's lighting volume is filled by a vertex shader, and that shader's writes were
going nowhere. Native Vulkan is in the launcher again as experimental. Known problems still being worked on:
- some glow effects and the hit flash can turn the whole screen white;
- freezes while it builds shaders for the first time, at doors especially;
- it does not hold 60 FPS as steadily as the other renderers yet.
OpenGL and Vulkan output remain the renderers to play on.

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
