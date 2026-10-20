# SPDX-License-Identifier: GPL-2.0-or-later
# Builds Release with MSVC + Ninja, runs the test suite, then the benchmark.
#   powershell -File tools/bench.ps1 [-Reps 7] [-BuildDir build]
param([int]$Reps = 7, [string]$BuildDir = "build")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vc = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vc`" >nul 2>nul && cd /d `"$root`" && cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release >nul && cmake --build $BuildDir && $BuildDir\zcode_tests.exe --gtest_brief=1 && $BuildDir\zcode_bench.exe $Reps"
exit $LASTEXITCODE
