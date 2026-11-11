# Next steps: the launcher-side half

A community note, Apache-2.0 like the rest of the tools here. **None of this is built or tested** - see
"Why this is a note and not a patch" at the end. The script in this folder is the working half.

The idea: the crash handler already writes a report. The launcher should notice one happened and pack
the right files, so a player has one press to make instead of four questions to answer.

## Split it in two, because the halves have very different risk

**v1 - notice the crash, widen the bundle.** No new Win32 API, no subprocess. Small and boring.
**v2 - add the Event Viewer entry.** Needs a subprocess and its output captured; see the warning.

v1 is the part that changes behaviour for everyone. v2 is a nice-to-have that can wait for a build.

## v1: notice the crash

There is only one signal, and it is already written: the runtime logs
`*** Crash (signal ...)` from `main.c:240`. The exit code is not usable (the process is killed by the
driver), so reading the log is the detector. `plat.c` keeps five of them, which is why this works at all.

A helper beside the other file helpers in `fe_sys.cpp` (there is `file_size()` at line 33 but no time):

```cpp
long long file_mtime(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(wide(p).c_str(), GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
```

Then, next to `make_report()` (fe_sys.cpp:347), the scan. It answers "the newest crash, if it is newer
than the one already acknowledged":

```cpp
struct LastCrash { bool found = false; std::string log, dll, off; long long mtime = -1; };

LastCrash last_crash() {
    LastCrash c;
    const std::string n = P.exename;
    std::string names[] = {n + ".log", n + ".previous.log", n + ".previous2.log",
                           n + ".previous3.log", n + ".previous4.log"};
    for (auto& f : names) {
        std::string p = join(P.exedir, f);
        if (!file_exists(p)) continue;
        long long mt = file_mtime(p);
        if (mt <= c.mtime) continue;                  // older than the best so far: skip the read
        std::string s = read_text(p);
        size_t k = s.find("*** Crash (signal");
        if (k == std::string::npos) continue;
        c.found = true; c.log = f; c.mtime = mt;
        size_t e = s.find("\n", k); std::string head = s.substr(k, e == std::string::npos ? 120 : e - k);
        if (size_t d = head.find("in "); d != std::string::npos) {
            size_t par = head.find("(", d), close = head.find(")", par);
            if (par != std::string::npos && close != std::string::npos) {
                c.dll = head.substr(d + 3, par - d - 3);
                c.off = head.substr(par + 1, close - par - 1);
            }
        }
    }
    return c;
}
```

**Do not nag.** Store the acknowledged time in a small file and compare - once per launch, not once per
frame, and never twice for the same crash:

```cpp
std::string ack_path() { return join(reports_dir(), "last-crash-seen.txt"); }
```

Then the row, in `fe.cpp` inside `section("PROBLEMS")` at **line 511**, above the existing
`"Report a problem"` row at 512:

```cpp
section("PROBLEMS");
LastCrash lc = last_crash();                                  // cache it: this runs every frame
if (lc.found && file_mtime(ack_path()) < lc.mtime) {
    if (row_button("##crash", fmt("Your last run crashed (%s). Send that report?", lc.dll.c_str()).c_str(), "REPORT", true)) {
        report_problem();
        write_text(ack_path(), lc.log);
    }
}
```

`report_problem()` already does the rest - it packs the zip, opens the folder and opens the prefilled
issue page. The only behaviour change is that it is offered at the right moment and named after the
actual crash.

**The one-line bundle fix**, in `make_report()` at `fe_sys.cpp:350`. Today it takes `.log` and
`.previous.log`; after a restart the crashed run is usually neither:

```cpp
const std::string files[] = {n + ".log", n + ".previous.log", n + ".previous2.log",
                             n + ".previous3.log", n + ".previous4.log", "performance.txt",
                             "threads.txt", "setup.log", n + ".ini", "update.txt",
                             "vulkan_native\\building.txt"};
```

Five logs at ~12 KB each is still a small zip. `building.txt` is 8 KB and is the file that answers the
AMD shader question, so it is worth carrying even when blank - "blank" is itself the answer.

## v2: the Event Viewer entry

The Windows `Application` log holds an Event 1000 entry per crash with the faulting module, its version
and the fault offset - the thing asked for by hand in issue #1, comment 14. It reads without admin.

It needs a subprocess (`wevtutil qe Application /q:"*[System[(EventID=1000)]]" /c:5 /rd:true /f:text`)
and something that captures its output.

**The warning:** there is no reusable run-and-capture helper in the launcher today. The `CreatePipe` at
`fe_sys.cpp:210` is bespoke to `setup_start()` - it pumps through a reader thread into a job object.
A naive `CreateProcess` + `ReadFile` deadlocks once the child fills the pipe buffer, and a deadlocked
launcher on startup is worse than a missing Event Viewer entry. Either lift the reader-thread pattern
out of `setup_start()`, or shell out through `cmd /c ... > file` and read the file back.

That is why v2 is separate. Do v1, ship it, and v2 can be done properly with a build to test it.

## Optional: how far each run got

Cheap, and it answers a different question. When a run dies, "it reached the main menu" and "it reached
the first 3D frame" are different bugs - the maintainer was describing exactly this himself ("Vulkan
Native worked 100% for like 3 minutes, then the test closed").

A handful of one-line `LOG(...)` calls at the few places that already exist (`splash`, first window,
first frame, first save load) gives every report a shape for free, with no screenshot machinery and no
size cost. Screenshots are the expensive version of the same idea: `F8` writes `.ppm`, 11 MB each.

## Why this is a note and not a patch

The launcher cannot be built without three things that are not in the repository:

- `gcc\` - winlibs GCC 14.2.0 (UCRT, POSIX threads, x64)
- `python\` - Python 3.12 for Windows
- `glslang.exe` 16.6.0 on `PATH`

`build.bat` checks all three and stops otherwise, and none of them is on the machine this was written
on. The project rule is that pull requests which were not built or tested get closed - correctly, since
unbuilt C++ in a renderer is how a working build becomes a broken one.

So the code above is written to be applied by someone with the toolchain, and the v1/v2 split is there
so the part that needs no testing-of-the-renderer (`file_mtime`, `last_crash`, the row, the wider
array) can land on its own.
