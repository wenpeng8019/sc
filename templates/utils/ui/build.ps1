#!/usr/bin/env pwsh
# ============================================================
# build.ps1 -- ui module library build (thin scc wrapper; PowerShell port of build.sh)
#
# Platform backend macros live in the module .sc segments (read directly by scc,
# see compiler.md 7.4/7.6): darwin=SC_UI_COCOA / linux=SC_UI_NK / windows=SC_UI_WIN32.
# All src/* are compiled together; non-selected backends self-empty via SC_UI_* guards.
#
# NOTE: ASCII-only on purpose. Windows PowerShell 5.1 reads BOM-less .ps1 files
# as the system ANSI code page, which corrupts UTF-8 (CJK) comments and breaks
# parsing. Keep this file ASCII so it runs under both powershell 5.1 and pwsh 7.
#
# Usage:
#   ./build.ps1                                     # host build -> libui.a
#   ./build.ps1 --target ..\..\targets\xx.target    # cross -> libui.<suffix|triple>.a
# ============================================================
# NOTE: do NOT use $ErrorActionPreference='Stop' here. scc writes build progress
# to stderr; under 'Stop' PowerShell turns the first native-command stderr line
# into a terminating error. bash's `set -e` only reacts to exit codes.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

# scc: env var SCC > in-repo build product > PATH
$Scc = $env:SCC
if (-not $Scc) {
    $cand = Join-Path $ScriptDir '..\..\..\compiler\build\scc.exe'
    if (Test-Path $cand) { $Scc = (Resolve-Path $cand).Path }
}
if (-not $Scc) {
    $cmd = Get-Command scc -ErrorAction SilentlyContinue
    if ($cmd) { $Scc = $cmd.Source }
}
if (-not $Scc) {
    Write-Error 'scc not found (build the compiler first or set $env:SCC=<path>)'
    exit 1
}

& $Scc . --build @args
exit $LASTEXITCODE
