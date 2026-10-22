#Requires -Version 7
# Builds and runs the unit tests with Windhawk's bundled clang, or clang++ on
# PATH when Windhawk is not installed. Pass -Source to test another copy of the
# mod source. Build output goes to tests\build\.
param([string]$Source)

$ErrorActionPreference = 'Stop'
$clang = Join-Path $env:ProgramFiles 'Windhawk\Compiler\bin\clang++.exe'
if (-not (Test-Path $clang)) {
    $clang = (Get-Command clang++ -ErrorAction SilentlyContinue)?.Source
    if (-not $clang) { throw "clang++ was not found in Windhawk's install folder or on PATH" }
}

$tests = $PSScriptRoot
$build = Join-Path $tests 'build'
New-Item -ItemType Directory -Force $build | Out-Null
$exe = Join-Path $build 'mod_test.exe'

$defines = $Source ? @("-DMOD_SOURCE=`"$((Resolve-Path $Source).Path -replace '\\', '/')`"") : @()
& $clang -std=c++20 -Wall -I (Join-Path $tests 'shim') @defines `
    (Join-Path $tests 'mod_test.cpp') -o $exe -static -lshell32 -lole32
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $exe
exit $LASTEXITCODE
