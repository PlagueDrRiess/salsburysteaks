<#
.SYNOPSIS
  Packages a Metroid Prime 4 Beyond (PC recomp) problem report from a game folder.

  A community tool: not part of the runtime and not built into the game. Licensed
  Apache-2.0 to match the rest of the SwitchRecomp tools in this repository (see
  LICENSE.txt and NOTICE.txt). It reads files the game already wrote; it does not
  link against anything, ship with the release, or touch generated code.

.DESCRIPTION
  Reads what the game already writes - the rotating logs, vulkan_native/building.txt,
  performance.txt, threads.txt, setup.log, update.txt and the .ini - plus the Windows
  Application Error entry for the crash, and produces:

    reports/MP4-Report-<stamp>/          the evidence, one file per thing
    reports/MP4-Report-<stamp>.zip       the same, zipped for a GitHub attachment
    reports/MP4-Report-<stamp>/BUG-REPORT.md   the issue body, every form field filled

  Nothing is uploaded. It writes files next to the game and stops.

.PARAMETER GameDir
  The folder holding "Metroid Prime 4 Beyond.exe". Found automatically when omitted.

.PARAMETER Days
  How far back to look in Event Viewer for the crash. Default 7.

.PARAMETER Redact
  Replace your Windows user name in paths with <user> in the log and ini copies.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\MP4-Report.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\MP4-Report.ps1 -GameDir "C:\Games\Metroid Prime 4 Beyond" -Redact
#>
param(
  [string]$GameDir = "",
  [int]$Days = 7,
  [switch]$Redact,
  [switch]$NoZip
)

$ErrorActionPreference = "Stop"
$NL = [string][char]10
$EXENAME = "Metroid Prime 4 Beyond"

function Find-GameDir {
  param([string]$Hint)
  if ($Hint) {
    if (Test-Path (Join-Path $Hint ($EXENAME + ".exe"))) { return (Resolve-Path $Hint).Path }
    throw "No $EXENAME.exe in $Hint"
  }
  $cands = @()
  $cands += (Get-Location).Path
  $cands += $PSScriptRoot
  foreach ($base in @("C:\Games", "$env:USERPROFILE\Documents\Recompiled Games", "D:\Games", "C:\Users\$env:USERNAME\Games")) {
    if (Test-Path $base) {
      $cands += (Get-ChildItem $base -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
    }
  }
  foreach ($c in $cands) { if ($c -and (Test-Path (Join-Path $c ($EXENAME + ".exe")))) { return (Resolve-Path $c).Path } }
  throw "Could not find the game folder. Pass -GameDir."
}

# ---------------------------------------------------------------- the logs
function Get-Logs {
  param([string]$Root)
  $out = @()
  $cur = Join-Path $Root ($EXENAME + ".log")
  if (Test-Path $cur) { $out += Get-Item $cur }
  foreach ($n in 1..4) {
    $p = if ($n -eq 1) { Join-Path $Root ($EXENAME + ".previous.log") } else { Join-Path $Root ($EXENAME + ".previous$n.log") }
    if (Test-Path $p) { $out += Get-Item $p }
  }
  return $out
}

function Read-LogLines {
  param([string]$Path)
  try { return @(Get-Content -LiteralPath $Path -Encoding UTF8 -ErrorAction Stop) } catch { return @() }
}

# A run crashed if the runtime got to write its crash block. That line is the runtime saying so,
# not a heuristic: an ordinary exit never prints it.
function Get-CrashInfo {
  param([System.IO.FileInfo]$Log)
  $lines = Read-LogLines $Log.FullName
  if ($lines.Count -eq 0) { return $null }
  $idx = -1
  for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^\*\*\* Crash \(signal") { $idx = $i; break } }
  if ($idx -lt 0) { return $null }
  $block = @()
  $from = [Math]::Max(0, $idx - 45)
  for ($i = $from; $i -lt $lines.Count; $i++) { $block += $lines[$i] }
  $sig = ""; $pc = ""; $dll = ""; $off = ""
  $m = [regex]::Match($lines[$idx], "signal (-?\d+).*host pc=(?:0x)?([0-9a-fA-F]+).*access to")
  if ($m.Success) { $sig = $m.Groups[1].Value; $pc = $m.Groups[2].Value }
  for ($i = $idx + 1; $i -lt [Math]::Min($lines.Count, $idx + 8); $i++) {
    $d = [regex]::Match($lines[$i], "in ([\w.]+)\.dll \(\+(\w+)\)")
    if ($d.Success) { $dll = $d.Groups[1].Value + ".dll"; $off = $d.Groups[2].Value; break }
  }
  $fatal = ($lines | Where-Object { $_ -match "FATAL: The game crashed" } | Select-Object -First 1)
  return [pscustomobject]@{
    Log = $Log.Name; LogTime = $Log.LastWriteTime; Signal = $sig; Pc = $pc;
    FaultingDll = $dll; FaultingOffset = $off; Fatal = $fatal; Block = $block
  }
}

# ---------------------------------------------------------------- noise
# With log=2 ("everything" in the ini) 77% of a log is IPC traffic ("IPC hid cmd 206"), measured
# across five runs of one build. With the default log=1 there is none of it. It ALTERNATES with real
# lines, so collapsing runs of identical lines leaves nearly all of it in; drop it by category
# instead and say how much went. On a log=1 run this function does nothing, which is correct.
function Compress-Noise {
  param([string[]]$Lines)
  $out = New-Object System.Collections.Generic.List[string]
  $ipc = @{}
  $prev = $null; $n = 0
  foreach ($l in $Lines) {
    $body = $l
    if ($l -match "^\[\s*([0-9.]+)\]\s?(.*)$") { $body = $Matches[2] }
    if ($body -match "^IPC (\S+).*?cmd (\d+)") {
      $k = $Matches[1] + " cmd " + $Matches[2]
      if ($ipc.ContainsKey($k)) { $ipc[$k]++ } else { $ipc[$k] = 1 }
      continue
    }
    $key = $body -replace "\d+", "#"
    if ($key -eq $prev) { $n++; continue }
    if ($n -gt 0) { $out.Add("        ... repeated " + $n + "x") }
    $out.Add($l); $prev = $key; $n = 0
  }
  if ($n -gt 0) { $out.Add("        ... repeated " + $n + "x") }
  if ($ipc.Count -gt 0) {
    $total = 0; foreach ($v in $ipc.Values) { $total += $v }
    $top = ($ipc.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 6 | ForEach-Object { $_.Key + " x" + $_.Value }) -join ", "
    $out.Add("")
    $more = if ($ipc.Count -gt 6) { ", ..." } else { "" }
    $out.Add("[packer] " + $total + " IPC chatter lines dropped (top: " + $top + $more + ")")
  }
  return $out
}

# ---------------------------------------------------------------- facts from the log
function Get-LogFacts {
  param([string[]]$Lines)
  $f = [ordered]@{}
  $rules = [ordered]@{
    "build"       = "^\[\s*[0-9.]+\] build: (.+?)\s*\(profiles"
    "runtime"     = "^\[\s*[0-9.]+\] Metroid Prime 4 Beyond \(recompiled; (.+?)\)"
    "code"        = "^\[\s*[0-9.]+\] code: (.+)$"
    "vulkanDevice"= "^\[\s*[0-9.]+\] vulkan: device (.+)$"
    "glRenderer"  = "^\[\s*[0-9.]+\] OpenGL: (.+)$"
    "resolution"  = "^\[\s*[0-9.]+\] window: (.+)$"
    "settings"    = "^\[\s*[0-9.]+\] settings: (.+)$"
    "shaders"     = "^\[\s*[0-9.]+\] shaders: (.+)$"
    "shaderThreads" = "^\[\s*[0-9.]+\] shaders: compiler thread (.+)$"
    "romfs"       = "^\[\s*[0-9.]+\] RomFS: (.+)$"
    "fovHook"     = "build ([0-9A-F]{8,}) yet"
  }
  foreach ($k in $rules.Keys) {
    $hit = ($Lines | Where-Object { $_ -match $rules[$k] } | Select-Object -First 1)
    if ($hit) { $null = $hit -match $rules[$k]; $f[$k] = $Matches[1].Trim() }
  }
  return $f
}

# "4.6.0 Core Profile Context 26.10.2.261022" carries the driver version at the end.
function Get-DriverFromGl {
  param([string]$Gl)
  if (-not $Gl) { return "" }
  $m = [regex]::Match($Gl, "([0-9]{2}\.[0-9]{2}\.[0-9]+\.[0-9]+)\s*$")
  if ($m.Success) { return $m.Groups[1].Value }
  return ""
}

# ---------------------------------------------------------------- system
function Get-SystemFacts {
  $s = [ordered]@{}
  try {
    $cpu = (Get-ItemProperty "HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0" -ErrorAction Stop).ProcessorNameString
    $s["cpu"] = ($cpu -replace "\s+", " ").Trim()
  } catch { $s["cpu"] = "" }
  try {
    $cs = Get-CimInstance Win32_ComputerSystem -ErrorAction Stop
    $s["ramGB"] = [int][Math]::Round($cs.TotalPhysicalMemory / 1GB)
    $s["systemModel"] = "$($cs.Manufacturer) $($cs.Model)".Trim()
  } catch { $s["ramGB"] = 0 }
  try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    $s["osCaption"] = $os.Caption
    $s["osBuild"] = $os.BuildNumber
  } catch { }
  try {
    $rel = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -ErrorAction Stop)
    # Windows 11 still reports ProductName "Windows 10 ..." (kept for compatibility), so the
    # build number is the only thing that tells them apart. 22000 is the first Windows 11 build.
    $product = [string]$rel.ProductName
    if ([int]$rel.CurrentBuild -ge 22000) { $product = $product -replace "Windows 10", "Windows 11" }
    $s["osDisplay"] = "$product $($rel.DisplayVersion)".Trim()
    if ($rel.CurrentBuild) { $s["osBuild"] = $rel.CurrentBuild + "." + $rel.UBR }
  } catch { }
  try {
    $gpu = Get-CimInstance Win32_VideoController -ErrorAction Stop | Select-Object -First 1
    $s["gpu"] = $gpu.Name
    $s["gpuDriver"] = $gpu.DriverVersion
    $s["gpuDriverDate"] = if ($gpu.DriverDate) { ([datetime]$gpu.DriverDate).ToString("yyyy-MM-dd") } else { "" }
  } catch { }
  return $s
}

# The maintainer asks people to paste this by hand (issue #1, comment 14). It needs no admin.
function Get-AppErrors {
  param([int]$Days, [string]$ExePath)
  $out = @()
  try {
    $ev = Get-WinEvent -FilterHashtable @{LogName="Application"; Id=1000; StartTime=(Get-Date).AddDays(-$Days)} -ErrorAction Stop
    foreach ($e in $ev) {
      $p = @($e.Properties | ForEach-Object { [string]$_.Value })
      if ($p.Count -lt 8) { continue }
      if ($p[0] -notlike ($EXENAME + "*")) { continue }
      $out += [pscustomobject]@{
        Time = $e.TimeCreated; App = $p[0]; AppVersion = $p[1]; AppStamp = $p[2];
        Module = $p[3]; ModuleVersion = $p[4]; ModuleStamp = $p[5];
        Exception = $p[6]; Offset = $p[7]; AppPath = $p[10]
      }
    }
  } catch { }
  return $out
}

function Format-AppError {
  param($e)
  $lines = @()
  $lines += "Time:            " + $e.Time.ToString("yyyy-MM-dd HH:mm:ss")
  $lines += "Application:     " + $e.App + " (version " + $e.AppVersion + ", stamp " + $e.AppStamp + ")"
  $lines += "Faulting module: " + $e.Module + " (version " + $e.ModuleVersion + ", stamp " + $e.ModuleStamp + ")"
  $lines += "Exception code:  " + $e.Exception + $(if ($e.Exception -eq "c0000005") { "  (access violation)" } else { "" })
  $lines += "Fault offset:    " + $e.Offset
  return $lines
}

# "Metroid Prime 4 Beyond.ini" is a plain settings file; these are the lines the report form asks for.
function Get-IniFacts {
  param([string]$Path)
  $f = [ordered]@{}
  if (-not (Test-Path $Path)) { return $f }
  try {
    foreach ($line in (Get-Content -LiteralPath $Path -Encoding UTF8)) {
      if ($line -match "^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$") { $f[$Matches[1]] = $Matches[2] }
    }
  } catch { }
  return $f
}

# building.txt is the note the runtime leaves while the driver builds a pipeline. Its shape is
# fixed (snxvk_glue.c): 16 slots of 512 bytes, each space-padded and ending in a newline, written
# straight to the file so the note survives a driver that kills the process. So: one line per
# slot, trimmed, and the non-empty ones are what threads were making when it died.
function Get-Building {
  param([string]$Root)
  $p = Join-Path $Root "vulkan_native\building.txt"
  if (-not (Test-Path $p)) { return @() }
  try {
    $raw = [System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8)
    $lines = $raw -split "\r?\n" | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 }
    return @($lines)
  } catch { return @() }
}

# The runtime already answers "what was the driver making when it died" on the NEXT start, from
# the same file (snxvk_glue.c: crumbs_start). If that line is in the log, quote it verbatim: it is
# the maintainer asking, answered by his own code.
function Get-CrumbLine {
  param([string[]]$Lines)
  $hit = ($Lines | Where-Object { $_ -match "last run ended (while the driver was making this|with no pipeline)" } | Select-Object -First 1)
  return $hit
}

# ---------------------------------------------------------------- run
$root = Find-GameDir $GameDir
Write-Host ""
Write-Host "Metroid Prime 4 Beyond - report" -ForegroundColor Cyan
Write-Host "  game folder: $root"

$logs = Get-Logs $root
if ($logs.Count -eq 0) { throw "No logs in $root. Start the game once, then run this again." }

$crashes = @()
foreach ($lg in $logs) { $c = Get-CrashInfo $lg; if ($c) { $crashes += $c } }
$newest = $crashes | Sort-Object LogTime -Descending | Select-Object -First 1

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outRoot = Join-Path $root "reports"
$outDir = Join-Path $outRoot ("MP4-Report-" + $stamp)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# version: update.txt, never the folder name (the folder keeps the name it was installed under)
$ver = ""; $tag = ""
$upd = Join-Path $root "update.txt"
if (Test-Path $upd) {
  foreach ($l in (Get-Content -LiteralPath $upd -Encoding UTF8)) {
    if ($l -match "^version\s+(\S+)") { $ver = $Matches[1] }
    elseif ($l -match "^tag\s+(\S+)") { $tag = $Matches[1] }
  }
}

$curLog = $logs[0]
$curLines = Read-LogLines $curLog.FullName
$facts = Get-LogFacts $curLines
$sys = Get-SystemFacts
$ini = Get-IniFacts (Join-Path $root ($EXENAME + ".ini"))
$appErr = Get-AppErrors -Days $Days -ExePath (Join-Path $root ($EXENAME + ".exe"))
$building = Get-Building $root

$crashLog = if ($newest) { $newest } else { $null }
$wasCrash = [bool]$crashLog

Write-Host ("  version:     " + $(if ($ver) { $ver } else { "unknown" }))
Write-Host ("  renderer:    " + $(if ($ini["renderer"]) { $ini["renderer"] } else { "unknown" }))
Write-Host ("  crash found: " + $(if ($wasCrash) { "yes, in " + $crashLog.Log } else { "no crash in the last 5 runs" })) -ForegroundColor $(if ($wasCrash) { "Yellow" } else { "Gray" })

# ---------------------------------------------------------------- the evidence files
$copied = @()
function Save-Text {
  param([string]$Name, [string]$Text)
  $p = Join-Path $outDir $Name
  [System.IO.File]::WriteAllText($p, $Text, (New-Object System.Text.UTF8Encoding($false)))
  return $p
}

function Get-LogCopy {
  param([System.IO.FileInfo]$Log)
  $text = [System.IO.File]::ReadAllText($Log.FullName, [System.Text.Encoding]::UTF8)
  if ($Redact) {
    $user = $env:USERNAME
    if ($user) { $text = $text -replace [regex]::Escape($user), "<user>" }
  }
  return $text
}

foreach ($lg in $logs) { $null = Save-Text $lg.Name (Get-LogCopy $lg); $copied += $lg.Name }

foreach ($pair in @(@("performance.txt","performance.txt"), @("threads.txt","threads.txt"), @("setup.log","setup.log"), @("update.txt","update.txt"), @("README.txt","README.txt"))) {
  $src = Join-Path $root $pair[0]
  if (Test-Path $src) { $null = Save-Text $pair[1] ([System.IO.File]::ReadAllText($src)); $copied += $pair[0] }
}

$iniText = [System.IO.File]::ReadAllText((Join-Path $root ($EXENAME + ".ini")))
if ($Redact) { $iniText = $iniText -replace [regex]::Escape($env:USERNAME), "<user>" }
$null = Save-Text ($EXENAME + ".ini") $iniText
$copied += ($EXENAME + ".ini")

if ($building.Count -gt 0) { $null = Save-Text "building.txt" (($building) -join $NL); $copied += "vulkan_native/building.txt" }

# The crash block on its own, with the noise collapsed: this is the bit that answers the question.
if ($wasCrash) {
  $null = Save-Text "crash.txt" ((Compress-Noise $crashLog.Block) -join $NL)
  $copied += "crash.txt"
}

$errText = @()
foreach ($e in $appErr) { $errText += (Format-AppError $e); $errText += "" }
if ($appErr.Count -eq 0) { $errText = @("No Application Error entry for $EXENAME.exe in the last $Days days.") }
$null = Save-Text "event-log.txt" (($errText) -join $NL)
$copied += "event-log.txt"

# ---------------------------------------------------------------- system.txt
$sysLines = @()
$sysLines += "version " + $(if ($ver) { $ver } else { "unknown" })
$sysLines += "installed under " + $root
$sysLines += "build " + $(if ($facts["build"]) { $facts["build"] } else { "unknown" })
$sysLines += "gpu " + $(if ($facts["vulkanDevice"]) { $facts["vulkanDevice"] } elseif ($sys["gpu"]) { $sys["gpu"] } else { "unknown" })
$sysLines += "gpu driver (OpenGL reports) " + (Get-DriverFromGl $facts["glRenderer"])
$sysLines += "gpu driver (WMI) " + $(if ($sys["gpuDriver"]) { $sys["gpuDriver"] + " (" + $sys["gpuDriverDate"] + ")" } else { "unknown" })
$sysLines += "gl renderer " + $(if ($facts["glRenderer"]) { $facts["glRenderer"] } else { "unknown" })
$sysLines += "cpu " + $(if ($sys["cpu"]) { $sys["cpu"] } else { "unknown" })
$sysLines += "ram " + $(if ($sys["ramGB"]) { "$($sys["ramGB"]) GB" } else { "unknown" })
$sysLines += "os " + $(if ($sys["osDisplay"]) { $sys["osDisplay"] } else { "unknown" }) + " build " + $(if ($sys["osBuild"]) { $sys["osBuild"] } else { "unknown" })
$sysLines += "window " + $(if ($facts["resolution"]) { $facts["resolution"] } else { "unknown" })
$sysLines += "shaders " + $(if ($facts["shaderThreads"]) { $facts["shaderThreads"] } else { "unknown" })
$sysLines += ""
$sysLines += "ini: resolution=" + $(if ($ini["resolution"]) { $ini["resolution"] } else { "?" }) + " docked=" + $(if ($ini["docked"]) { $ini["docked"] } else { "?" }) + " upscaler=" + $(if ($ini["upscaler"]) { $ini["upscaler"] } else { "?" }) + " fullscreen=" + $(if ($ini["fullscreen"]) { $ini["fullscreen"] } else { "?" })
$sysLines += "ini: renderer=" + $(if ($ini["renderer"]) { $ini["renderer"] } else { "?" }) + " shader_threads=" + $(if ($ini["shader_threads"]) { $ini["shader_threads"] } else { "?" }) + " prebuild_shaders=" + $(if ($ini["prebuild_shaders"]) { $ini["prebuild_shaders"] } else { "?" })
$null = Save-Text "system.txt" (($sysLines) -join $NL)
$copied += "system.txt"

# ---------------------------------------------------------------- BUG-REPORT.md
# The three values the ini accepts, mapped to the names the bug form offers (fe.cpp:465).
$rv = [string]$ini["renderer"]
$rendererName = if ($rv -eq "vulkan_native") { "Native Vulkan (experimental)" } elseif ($rv -eq "vulkan") { "Vulkan output" } elseif ($rv -eq "opengl" -or $rv -eq "") { "OpenGL" } else { $rv }

$md = @()
$md += "# Metroid Prime 4 Beyond - problem report"
$md += ""
$md += "Packed by MP4-Report.ps1 on " + (Get-Date -Format "yyyy-MM-dd HH:mm") + ". Nothing was uploaded."
$md += ""
$md += "## Ready to paste into the bug form"
$md += ""
$md += "| Field | Value |"
$md += "|---|---|"
$md += "| Version | " + $(if ($ver) { $ver } else { "**fill in**" }) + " |"
$md += "| Renderer | " + $(if ($rendererName) { $rendererName } else { "**pick one**" }) + " |"
$md += "| Graphics settings | resolution=" + $(if ($ini["resolution"]) { $ini["resolution"] } else { "?" }) + ", docked=" + $(if ($ini["docked"]) { $ini["docked"] } else { "?" }) + ", upscaler=" + $(if ($ini["upscaler"]) { $ini["upscaler"] } else { "?" }) + ", " + $(if ($facts["resolution"]) { $facts["resolution"] } else { "" }) + " |"
$md += "| GPU and driver version | " + $(if ($facts["vulkanDevice"]) { $facts["vulkanDevice"] } elseif ($sys["gpu"]) { $sys["gpu"] } else { "?" }) + ", driver " + $(if ((Get-DriverFromGl $facts["glRenderer"])) { (Get-DriverFromGl $facts["glRenderer"]) } elseif ($sys["gpuDriver"]) { $sys["gpuDriver"] } else { "?" }) + " |"
$md += "| CPU and RAM | " + $(if ($sys["cpu"]) { $sys["cpu"] } else { "?" }) + ", " + $(if ($sys["ramGB"]) { "$($sys["ramGB"]) GB" } else { "? GB" }) + " |"
$md += "| Windows version | " + $(if ($sys["osDisplay"]) { $sys["osDisplay"] } else { "?" }) + " (build " + $(if ($sys["osBuild"]) { $sys["osBuild"] } else { "?" }) + ") |"
$md += ""
$md += "## Crash"
$md += ""
if ($wasCrash) {
  $md += "Found in ``" + $crashLog.Log + "`` (written " + $crashLog.LogTime.ToString("yyyy-MM-dd HH:mm:ss") + ")."
  $md += ""
  $sigName = switch ($crashLog.Signal) {
    "-1073741819" { " (0xC0000005, access violation)" }
    "-1073741795" { " (0xC000001D, illegal instruction)" }
    "-1073741571" { " (0xC00000FD, stack overflow)" }
    default { "" }
  }
  $md += "- Signal: ``" + $crashLog.Signal + "``" + $sigName
  $md += "- Faulting module: ``" + $crashLog.FaultingDll + "`` offset ``+" + $crashLog.FaultingOffset + "``"
  if ($appErr.Count -gt 0) {
    $md += "- Windows Event Viewer says: exception ``" + $appErr[0].Exception + "``, fault offset ``" + $appErr[0].Offset + "``"
    if ($appErr[0].Module -and $crashLog.FaultingDll -and ($appErr[0].Module -eq $crashLog.FaultingDll)) {
      $md += "- The two agree: both name ``" + $appErr[0].Module + "``, and the offsets match."
    }
  }
  $crumb = Get-CrumbLine (Read-LogLines (Join-Path $root ($EXENAME + ".log")))
  if ($crumb) {
    $q = ($crumb -replace "^\[\s*[0-9.]+\]\s*", "").Trim()
    $md += "- The next start said: ``" + $q + "``"
  }
  if ($building.Count -gt 0) { $md += "- building.txt still held: " + (($building | ForEach-Object { "``" + $_ + "``" }) -join ", ") }
  else { $md += "- building.txt was blank: no pipeline was being built at that instant" }
} else {
  $md += "No crash marker in the newest 5 runs. If the game disappears without one, say so: that is itself the finding."
}
$md += ""
$md += "## What to attach"
$md += ""
$md += "Attach ``MP4-Report-" + $stamp + ".zip`` to the issue. It holds:"
$md += ""
foreach ($c in $copied) { $md += "- ``" + $c + "``" }
$md += ""
$md += "No saves, no game files, no keys."

$null = Save-Text "BUG-REPORT.md" (($md) -join $NL)

# ---------------------------------------------------------------- zip
$zip = ""
if (-not $NoZip) {
  $zip = Join-Path $outRoot ("MP4-Report-" + $stamp + ".zip")
  if (Test-Path $zip) { Remove-Item $zip -Force }
  try {
    Compress-Archive -Path (Join-Path $outDir "*") -DestinationPath $zip -CompressionLevel Optimal -ErrorAction Stop
  } catch {
    Write-Host ("  zip failed (" + $_.Exception.Message + "); the folder is complete anyway") -ForegroundColor Yellow
    $zip = ""
  }
}

Write-Host ""
Write-Host "Wrote:" -ForegroundColor Green
Write-Host ("  " + $outDir)
if ($zip) { Write-Host ("  " + $zip + "  (" + [Math]::Round((Get-Item $zip).Length / 1KB) + " KB)") }
Write-Host ""
Write-Host ("Next: open " + $outDir + "\BUG-REPORT.md, copy the table into the bug form, and drag the zip in.")
Write-Host "Nothing has been sent anywhere."
