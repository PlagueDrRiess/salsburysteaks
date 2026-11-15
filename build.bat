@echo off
rem Builds the Metroid Prime 4 Beyond PC EXE from this repository (the steps in the README, with checks first).
rem   build.bat                  the release EXE in bin\runtime_win64_win32.exe
rem   build.bat release 0.2.2    also packs a release folder in release\ (needs redist\zlib1.dll and redist\SDL2.dll)
rem   build.bat dlss 0.2.2       the same with NVIDIA DLAA, packed in release\0.2.2-dlss\: for your own PC only, never
rem                              share it (GPL-3.0 EXE + NVIDIA's NGX). Put nvngx_dlss.dll in the game folder.
setlocal
cd /d "%~dp0"
set DLSS=
if /i "%1"=="dlss" set DLSS=1

if not exist "gcc\Library\bin\gcc.exe" (
  echo gcc\ is missing: put winlibs GCC 14.2.0 ^(UCRT, POSIX threads, x64^) in gcc\ so that gcc\Library\bin\gcc.exe exists.
  exit /b 1
)
if not exist "python\python.exe" (
  echo python\ is missing: put Python 3.12 for Windows ^(the embeddable package is enough^) in python\.
  exit /b 1
)
if "%GLSLANG%"=="" (
  for %%G in (glslang.exe) do set "GLSLANG=%%~$PATH:G"
)
if "%GLSLANG%"=="" (
  echo glslang 16.6.0 was not found: set GLSLANG=C:\path\to\glslang.exe or put glslang.exe on PATH.
  exit /b 1
)
if not exist "%GLSLANG%" (
  echo GLSLANG points to "%GLSLANG%", which does not exist.
  exit /b 1
)
echo Using %GLSLANG%

echo [1/4] Dear ImGui
python\python.exe work-profiler\build_imgui.py || goto :fail
if defined DLSS (
  echo [2/4] Native Vulkan renderer ^(with DLSS^)
  python\python.exe work-profiler\build_snxvk2.py || goto :fail
) else (
  echo [2/4] Native Vulkan renderer ^(no DLSS^)
  set SWR_NO_DLSS=1
  python\python.exe work-profiler\build_snxvk2.py || goto :fail
  set SWR_NO_DLSS=
)
echo [3/4] Runtime EXE
set SWR_RELEASE=1
if defined DLSS set SWR_WITH_DLSS=1
rem a public release never takes the private developer tools in runtime\local
if /i "%1"=="release" set SWR_PUBLIC=1
python\python.exe switchrecomp.py --build-runtime --cc gcc || goto :fail
set SWR_PUBLIC=
set SWR_RELEASE=
set SWR_WITH_DLSS=
if /i not "%1"=="release" if not defined DLSS (
  echo Done: bin\runtime_win64_win32.exe
  exit /b 0
)
if "%2"=="" (
  echo usage: build.bat release X.Y.Z   or   build.bat dlss X.Y.Z
  exit /b 1
)
set OUT=release\%2
if defined DLSS set OUT=release\%2-dlss
echo [4/4] Release %2 in %OUT%
python\python.exe beyond\make_release.py release %OUT% --version %2 --exe bin\runtime_win64_win32.exe || goto :fail
if defined DLSS (
  echo Done: %OUT%. Personal DLSS build: don't share it. Copy nvngx_dlss.dll into its game folder, then turn on
  echo Settings ^> Graphics ^> NVIDIA DLAA.
) else (
  echo Done: %OUT%
)
exit /b 0

:fail
echo Build failed: see the messages above.
exit /b 1
