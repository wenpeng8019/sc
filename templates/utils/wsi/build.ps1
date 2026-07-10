#!/usr/bin/env pwsh
# ============================================================
# build.ps1 -- wsi module library build (thin scc wrapper; PowerShell port of build.sh)
#
# Platform backend macros / include paths / link flags all live in the module
# .sc segments (read directly by scc; see compiler.md 7.4/7.6). This script only:
#   1. Linux target: run wayland-scanner over wayland-protocols/*.xml to generate
#      protocol headers into build/wayland-protocols (skipped for non-Linux
#      targets; Windows usually has no wayland-scanner).
#   2. Invoke the scc module-library build: scc . --build [passthrough args]
#
# NOTE: ASCII-only on purpose. Windows PowerShell 5.1 reads BOM-less .ps1 files
# as the system ANSI code page, which corrupts UTF-8 (CJK) comments and breaks
# parsing. Keep this file ASCII so it runs under both powershell 5.1 and pwsh 7.
#
# Usage:
#   ./build.ps1                                     # host build -> libwsi.a
#   ./build.ps1 --target ..\..\targets\xx.target    # cross -> libwsi.<suffix|triple>.a
#   $env:SCC_TARGET_TRIPLE = "aarch64-linux-gnu"; ./build.ps1
#
# Toolchain selection, variant naming, MSVC build, etc. all go through scc
# (SCC_CC/SCC_AR/--target target profiles; see compiler.md 5/6).
# ============================================================
# NOTE: do NOT use $ErrorActionPreference='Stop' here. scc writes build progress
# to stderr; under 'Stop' PowerShell turns the first native-command stderr line
# into a terminating error. bash's `set -e` only reacts to exit codes, so we
# mirror that by checking exit codes / $LASTEXITCODE instead.
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

# ---- Wayland protocol header generation (Linux target only) ----
# Target-family probe: SCC_TARGET_TRIPLE / arg string contains "linux" -> generate
# (a host Windows build has no "linux" keyword, so this is skipped).
$probe = "$($env:SCC_TARGET_TRIPLE)$($args -join ' ')"
if ($probe -match 'linux') {
    $scanner = Get-Command wayland-scanner -ErrorAction SilentlyContinue
    if (-not $scanner) {
        Write-Error 'Linux target needs wayland-scanner (usually unavailable on Windows; build on Linux/WSL)'
        exit 1
    }
    $WlProtoDir = 'build\wayland-protocols'
    if (Test-Path $WlProtoDir) { Remove-Item -Recurse -Force $WlProtoDir }
    New-Item -ItemType Directory -Force -Path $WlProtoDir | Out-Null
    # <generated base name> = <XML file name> (base name matches wl_*.c #include verbatim)
    $WlProtos = [ordered]@{
        'wayland-client-protocol'                         = 'wayland.xml'
        'xdg-shell-client-protocol'                       = 'xdg-shell.xml'
        'xdg-decoration-unstable-v1-client-protocol'      = 'xdg-decoration-unstable-v1.xml'
        'viewporter-client-protocol'                      = 'viewporter.xml'
        'relative-pointer-unstable-v1-client-protocol'    = 'relative-pointer-unstable-v1.xml'
        'pointer-constraints-unstable-v1-client-protocol' = 'pointer-constraints-unstable-v1.xml'
        'fractional-scale-v1-client-protocol'             = 'fractional-scale-v1.xml'
        'xdg-activation-v1-client-protocol'               = 'xdg-activation-v1.xml'
        'idle-inhibit-unstable-v1-client-protocol'        = 'idle-inhibit-unstable-v1.xml'
    }
    foreach ($base in $WlProtos.Keys) {
        $xml = Join-Path 'wayland-protocols' $WlProtos[$base]
        & $scanner.Source client-header $xml (Join-Path $WlProtoDir "$base.h")
        & $scanner.Source private-code  $xml (Join-Path $WlProtoDir "$base-code.h")
    }
    Write-Host "wayland protocol headers generated -> $WlProtoDir"
}

& $Scc . --build @args
exit $LASTEXITCODE
