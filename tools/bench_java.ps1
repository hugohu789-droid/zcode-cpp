# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the Java original on the same workloads as bench/zcode_bench.cpp.
#   powershell -File tools/bench_java.ps1 [-Reps 15] [-Warmup 10] [-Original]
# -Original measures the unpatched original; the default is the patched build,
# which behaves like the current C++ library.
param([int]$Reps = 15, [int]$Warmup = 10, [switch]$Original)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gj = Join-Path $root "tools\golden-java"
$target = if ($Original) { "target-original" } else { "target" }
$profile = if ($Original) { @("-Poriginal") } else { @() }
mvn -q -B -f "$gj\pom.xml" @profile compile dependency:build-classpath "-Dmdep.outputFile=$target/classpath.txt"
$cp = "$gj\$target\classes;" + (Get-Content "$gj\$target\classpath.txt" -Raw).Trim()
java -cp $cp zcodecpp.golden.JavaBench (Join-Path $root "testdata") $Reps $Warmup
