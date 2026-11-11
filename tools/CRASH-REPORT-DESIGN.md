# Getting a better crash report out of a player, with less asking

*A community proposal, Apache-2.0 like the rest of the tools here. Nothing in it is part of the*
*runtime, and none of it has been built into the game.*

Written after reading issue #1 (29 comments) and the 2026.11.17 source. It is a proposal, not a
patch: the point is to show what the project already does, what is genuinely missing, and which
parts of "opt in and it sends itself" can be built without breaking PRIVACY.md.

## 1. What already exists (most of it)

The hard part is done. `runtime/main.c` installs a crash handler that catches the fault, walks the
stack, names the faulting module and thread, and writes the block a maintainer needs:

```
*** Crash (signal -1073741819) at host pc=... ***
  in atio6axx.dll (+714047), a thread of the runtime ...: a graphics driver's DLL here points at the driver
  thread 55432: GL-driver
  call chain, innermost first (+N = exe offset; ...)
    #0  atio6axx.dll+714047   ...   #10 +a12de
  called from the runtime at +a12de (frame #10)
FATAL: The game crashed (invalid memory access). See the .log.
```

`runtime/plat.c` rotates five logs, and says why: "a crash report needs them; the player restarts
the game to send it". `runtime/snxvk_glue.c` keeps `vulkan_native/building.txt` (16 slots of 512
bytes, written straight to disk, unflushed so it survives a driver that kills the process) and, on
the next start, prints "the last run ended while the driver was making this: ..." into the log.

`runtime/beyond_ui/fe.cpp:375 report_problem()` already packs a zip and opens a prefilled issue URL.
`fe_sys.cpp:347 make_report()` already has a stored-zip writer and picks up the logs, the ini,
`performance.txt`, `threads.txt`, `setup.log` and `update.txt`.

So roughly 80% of a crash reporter exists. What is missing is everything between "the process died"
and "a human decided to file it".

## 2. What is missing, concretely

1. **Nothing notices the crash.** `grep crash runtime/beyond_ui/*.cpp` finds nothing. The launcher
   cannot tell a crashed run from a closed one, so it cannot prompt. The player has to remember,
   find the button, find the folder, and drag the file.
2. **The bundle is thinner than the questions.** It carries `.log` and `.previous.log` only, not
   `.previous2..4` (the crashed run is often not the newest one after a restart), and not
   `building.txt`, which is the file built specifically to answer the AMD question.
3. **Nothing collects the Event Viewer entry**, which he asked for by hand in comment 14. It is
   readable without admin and it corroborates the log: the crash offset `714047` appears in both.
4. **The log swings 20x with one setting.** Measured across five runs of the same build on one
   install: at `log=1` (the default) a crashed run is ~190 lines and 12 KB; at `log=2` ("everything")
   it is ~3,700 lines and 141 KB, of which **77% is `IPC <service> cmd <n>`** chatter. So this is not
   a general complaint - it only bites the people who turned the detail up, and those are exactly the
   people filing the detailed reports. Worth knowing before either quoting "most of the log is noise"
   or building a feature around it.
5. **The metadata is entered by hand and drifts.** Issue #1 says version `2026.11.11`; `update.txt`
   in that same install says `2026.11.17`. The folder keeps the name it was installed under, so the
   obvious thing to copy is the wrong thing.

## 3. The three phases, easiest first

### Phase 1 - detect it, and pack the right things (no infrastructure, no policy change)

* In the launcher, on startup: scan the five logs for `*** Crash (signal`. If the newest crash is
  newer than the last time the launcher asked, show one row on the Play page: "Your last run
  crashed (atio6axx.dll, access violation). Send that report?" - the same one-click that exists,
  pointed at the right run, at the moment it is relevant.
* Widen `make_report()` to take all five logs plus `vulkan_native/building.txt`, and add the
  Event Viewer entry for the crash time (no admin needed, `Get-WinEvent -FilterHashtable`).
* Add a `crash.txt`: the ~45 lines around the marker with IPC chatter dropped by category. On a
  `log=1` run this is a no-op and costs nothing; on a `log=2` run it turns 3,700 lines into 30 and
  says what it dropped ("902 IPC chatter lines dropped (hid cmd 206 x846, ...)"). It is a convenience
  for the noisy case, not the headline - a default-length crash log is already small enough to read.
* Read the version from `update.txt`, never from the folder name.

This is the whole of `tools/MP4-Report.ps1` in this repo, working, against a real install. It is a
prototype of Phase 1 outside the launcher - useful now (it can be run by anyone with a game folder
and no build toolchain) and a specification for the C++ version.

### Phase 2 - make sending one click (still no server of ours)

GitHub decides this. An issue body can be prefilled from a URL; **an attachment cannot.**
Uploading needs a token, and a token in a shipped binary is a token everyone has.

Options that keep "no servers of ours" true:

| Option | What the player does | Cost |
|---|---|---|
| Paste the `crash.txt` body, attach the zip by hand | 2 steps | none (today) |
| The launcher copies a ready-made markdown body to the clipboard and opens the form | 1 paste + 1 drag | none |
| Upload the zip to a third-party paste host, put the link in the body | 1 paste | a dependency, and the logs leave through someone else |

The second is the honest ceiling without infrastructure, and it is worth reaching: it removes the
step people actually get wrong (transcribing system info).

### Phase 3 - the thing being asked for (a server, and a policy change)

"Opt in and it sends itself" means the software POSTs the bundle somewhere. That means a server,
and `PRIVACY.md` currently promises the opposite in as many words: *"The project has no accounts and
runs no servers of its own... Nothing is sent unless you submit that page yourself."* That sentence
is a feature. Any auto-send is a policy change, not just a code change, and the policy is what
makes the promise trustworthy.

If it is done, the shape that keeps it defensible:

* **Off by default, one explicit toggle**, and the toggle says what leaves the machine.
* **A random install id, not a user id.** Enough to correlate "this card, this version" without
  knowing who. Offer to reset it.
* **A receipt.** "Sent to the maintainer" with the exact file list, so it is observable rather than
  a claim.
* **Show what would be sent, before it is sent**, the first time.
* **A relay you control** (a Cloudflare Worker + object storage is ~free at this volume) rather
  than a token in the binary. The runtime POSTs the zip and gets an id; the issue text carries the id.
* **Size limit and a kill switch** server-side, because a crash loop can otherwise upload the same
  71 KB every 90 seconds.

## 4. The "milestones and screenshots" idea

Also asked: record the first 30 s, or a screenshot per milestone, and send that with a crash.

The pieces exist - `F8` writes a full-resolution screenshot, `F3` a normal one, `F5` a frame trace,
`runtime/backend_win32.c` owns `g_shot_req`. Two cautions:

* **Screenshots are `.ppm`, 11 MB each** on a 1440p install. Five of them for a 71 KB report is a
  40x cost, and `.ppm` cannot be previewed in a GitHub comment. Post-process to PNG/JPEG and cap
  the count before anything is attached.
* **A crash usually has no screenshot of itself** - the process is gone. What is useful is the
  *last good frame*, which means keeping the most recent one in a ring and writing it on crash.
  That is a real feature, not a freebie: the GPU thread is the thing that died.

Milestones are the cheaper half and worth more: a small ring of "reached main menu / loaded save /
first 3D frame / N minutes played" lines in the log answers "does it ever get further than last
time" without a single image. Several of the 29 comments are exactly that question.

## 5. What I would do first, in order

1. **Phase 1 in the launcher** (crash row + wider bundle + `crash.txt`). No policy change, no
   server, and it makes every report filed from then on comparable.
2. **The clipboard body.** Cheap, and it is where the transcription errors are.
3. **Only then** the upload, when there is a place to upload to. Until there is, the toggle cannot
   be built honestly, and `PRIVACY.md` should stay true.

## 6. If this is picked up

The one thing to be careful about: `make_report()` currently writes into the game folder
(`reports/`). An auto-pack on every crash start needs a cap, or a crash loop fills the disk with
identical 71 KB zips. Keep the newest N, or keep one per distinct crash signature.
