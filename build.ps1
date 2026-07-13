# build.ps1 - sc language one-shot script (Windows / PowerShell)
# Mirror of build.sh: build the scc compiler, run samples, install scc + VSCode extensions.
#
# Usage:
#   .\build.ps1 [build|dist|test|install|uninstall|clean|help]
#
# Notes (Windows specifics):
#   - Auto-enters a Visual Studio Developer Shell (MSVC cl) if not already in one.
#   - Prefers the Ninja generator (shipped with VS) so scc.exe lands at compiler\build\scc.exe.
#   - ASCII-only comments/strings on purpose: PowerShell 5.1 reads no-BOM scripts as the
#     system ANSI code page (GBK on zh-CN), which would garble non-ASCII text.
#   - Does NOT set $ErrorActionPreference='Stop': scc writes progress to stderr, which would
#     otherwise be treated as a terminating error. Exit codes are checked explicitly.

param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'dist', 'test', 'install', 'uninstall', 'clean', 'help', '-h', '--help')]
    [string]$Command = 'build',

    [Parameter(Position = 1)]
    [string]$Arg2 = ''
)

$Root      = $PSScriptRoot
$BuildDir  = Join-Path $Root 'compiler\build'
$DistDir   = Join-Path $Root 'compiler\build-dist'
$ExtBase   = Join-Path $env:USERPROFILE '.vscode\extensions'
$Prefix    = if ($env:PREFIX) { $env:PREFIX } else { Join-Path $env:LOCALAPPDATA 'Programs\scc' }
# extension source dir : installed name
$Exts = @(
    @{ Src = 'vscode-sc';  Name = 'sc-lang-0.1.0' },
    @{ Src = 'vscode-sg';  Name = 'sg-lang-0.1.0' },
    @{ Src = 'vscode-ast'; Name = 'sc-ast-view-0.1.0' }
)

function Fail([string]$msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Show-Usage {
    Write-Host @'
Usage: .\build.ps1 <command>

Commands:
  build      Build the scc compiler (default)
  dist       Build a release scc with embedded builtins (single self-contained binary)
  test       Build + run examples smoke tests + golden snapshot regression
             Add --update to regenerate golden files: .\build.ps1 test --update
  install    Install scc to %LOCALAPPDATA%\Programs\scc\bin (or $env:PREFIX\bin) and
             link the VSCode extensions (highlight + AST view + Markdown preview)
  uninstall  Remove scc and the VSCode extension links
  clean      Remove build outputs
'@
}

# Ensure a Visual Studio Developer environment (cl on PATH). Idempotent.
function Enter-DevShell {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    }
    if (-not $vsPath) {
        foreach ($p in @(
                'C:\Program Files\Microsoft Visual Studio\2022\Community',
                'C:\Program Files\Microsoft Visual Studio\2022\Professional',
                'C:\Program Files\Microsoft Visual Studio\2022\Enterprise',
                'C:\Program Files\Microsoft Visual Studio\2022\BuildTools')) {
            if (Test-Path $p) { $vsPath = $p; break }
        }
    }
    if (-not $vsPath) { Fail 'Visual Studio (with C++ tools) not found. Install VS 2022 or run from a Developer PowerShell.' }
    $dll = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module $dll
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -no_logo' | Out-Null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail 'Failed to enter the VS Developer Shell (cl not found).' }
}

# Locate the freshly built scc.exe under a build dir (Ninja -> <dir>\scc.exe, VS -> <dir>\Release\scc.exe).
function Find-Scc([string]$dir) {
    $p = Join-Path $dir 'scc.exe'
    if (Test-Path $p) { return $p }
    $hit = Get-ChildItem -Path $dir -Recurse -Filter 'scc.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# Pick a single-config generator that keeps scc.exe at <build>\scc.exe if available.
function Get-Generator {
    if (Get-Command ninja.exe -ErrorAction SilentlyContinue) { return 'Ninja' }
    return $null  # fall back to CMake default (multi-config VS)
}

function Invoke-Configure([string]$srcRel, [string]$outDir, [string[]]$extra) {
    $src = Join-Path $Root $srcRel
    $gen = Get-Generator
    $args = @('-B', $outDir, '-S', $src, '-DCMAKE_BUILD_TYPE=Release') + $extra
    if ($gen) { $args = @('-G', $gen) + $args }
    cmake @args
    if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }
}

function Do-Build {
    Write-Host '==> Building scc'
    Enter-DevShell
    Invoke-Configure 'compiler' $BuildDir @()
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { Fail 'CMake build failed.' }
    $scc = Find-Scc $BuildDir
    if (-not $scc) { Fail 'Build finished but scc.exe was not found.' }
    Write-Host "==> Done: $scc"
    return $scc
}

function Do-Dist {
    Write-Host '==> Building release scc (embedded builtins)'
    Enter-DevShell
    Invoke-Configure 'compiler' $DistDir @('-DSCC_EMBED_BUILTINS=ON')
    cmake --build $DistDir --config Release
    if ($LASTEXITCODE -ne 0) { Fail 'CMake dist build failed.' }
    $scc = Find-Scc $DistDir
    if (-not $scc) { Fail 'Dist build finished but scc.exe was not found.' }

    Write-Host '==> Verifying embedded builtins (run outside the repo dir)'
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("scc_dist_" + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    $sc = @'
inc adt.sc
inc mt.sc
inc mem.sc
fnc main: i4
    var s: string
    s.append("dist-ok")
    var mu: mutex
    mu.lock()
    var p: & = chunk(128)
    recycle(p)
    mem_teardown()
    printf("%s\n", s.cstr())
    mu.unlock()
    mu.drop()
    s.drop()
    return 0
'@
    Set-Content -Path (Join-Path $tmp 't.sc') -Value $sc -Encoding ASCII
    Push-Location $tmp
    & $scc 't.sc'
    $code = $LASTEXITCODE
    Pop-Location
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    if ($code -ne 0) { Fail 'Embedded-builtins smoke test failed.' }
    Write-Host "==> Done: $scc"
}

function Do-Test([string]$update) {
    $scc = Do-Build
    $ex  = Join-Path $Root 'examples'

    Write-Host '==> End-to-end examples/feature*.sc'
    $features = @(
        'feature1','feature2','feature3','feature4','feature5','feature6','feature7','feature8','feature9','feature10',
        'feature11','feature12','feature13','feature14','feature15','feature16','feature17','feature18','feature19','feature20',
        'feature21','feature22','feature23','feature24','feature25','feature26','feature27','feature28','feature29','feature31',
        'feature32','feature33','feature34','feature35','feature36','feature37','feature_forward')
    foreach ($f in $features) {
        Write-Host "--- $f.sc (default mode) ---"
        & $scc (Join-Path $ex "$f.sc")
        if ($LASTEXITCODE -ne 0) { Fail "$f.sc failed (exit $LASTEXITCODE)." }
    }
    Write-Host '--- feature30/feature30.sc (default mode) ---'
    & $scc (Join-Path $ex 'feature30\feature30.sc'); if ($LASTEXITCODE -ne 0) { Fail 'feature30 failed.' }
    Write-Host '--- feature38/feature38.sc (cross-module generics) ---'
    & $scc (Join-Path $ex 'feature38\feature38.sc'); if ($LASTEXITCODE -ne 0) { Fail 'feature38 failed.' }
    Write-Host '--- args_native/args_native.sc (@@ injection) ---'
    & $scc (Join-Path $Root 'tests\cases\args_native\args_native.sc') '--' '-v' '-n' '3' '-i' 'data.txt' '-f' 'a' 'b' 'c' 'x' 'y'
    if ($LASTEXITCODE -ne 0) { Fail 'args_native failed.' }
    Write-Host '--- test_demo.sc (--test mode; intentional failures expected) ---'
    & $scc (Join-Path $ex 'test_demo.sc') '--test'   # non-zero is expected here, do not fail

    Write-Host '--- feature1.sc (--emit-c) ---'
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("scc_test_" + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    & $scc (Join-Path $ex 'feature1.sc') '--emit-c' '-o' (Join-Path $tmp 'feature1.c')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $tmp 'feature1.c'))) { Fail 'emit-c did not produce feature1.c.' }
    Write-Host '    feature1.c generated (compile+run already validated by default mode above)'
    Write-Host '--- feature_export_inc.sc (--emit-c header) ---'
    & $scc (Join-Path $ex 'feature_export_inc.sc') '--emit-c' '-o' (Join-Path $tmp 'exp.c')
    if (Test-Path (Join-Path $tmp 'exp.h')) { Write-Host '    exp.h generated' }
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue

    Write-Host '--- feature_bad_value_cycle.sc (expected to error) ---'
    & $scc (Join-Path $ex 'feature_bad_value_cycle.sc') 2>$null
    if ($LASTEXITCODE -eq 0) { Fail 'negative case did not error as expected.' }
    Write-Host '    errored as expected'

    Write-Host '==> Product regression tests/run.sh'
    $bash = (Get-Command bash.exe -ErrorAction SilentlyContinue)
    if ($bash) {
        & $bash.Source (Join-Path $Root 'tests/run.sh') $update
        if ($LASTEXITCODE -ne 0 -and $update -ne '--update') { Fail 'tests/run.sh regression failed.' }
    }
    else {
        Write-Host '    SKIP: bash not found (Git Bash). Run tests/run.sh under WSL/Git Bash for golden regression.'
    }
    if ($update -eq '--update') { Write-Host '==> Golden files updated'; return }
    Write-Host '==> All checks passed'
}

function Do-Install {
    $scc = Do-Build
    $binDir = Join-Path $Prefix 'bin'
    Write-Host "==> Installing scc -> $binDir\scc.exe"
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    Copy-Item $scc (Join-Path $binDir 'scc.exe') -Force
    # Add bin dir to the user PATH if missing.
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($userPath -split ';') -notcontains $binDir) {
        [Environment]::SetEnvironmentVariable('Path', ($userPath.TrimEnd(';') + ';' + $binDir), 'User')
        Write-Host "    added $binDir to user PATH (restart terminals to pick it up)"
    }

    Write-Host '==> Installing VSCode extensions (directory junctions)'
    New-Item -ItemType Directory -Path $ExtBase -Force | Out-Null
    foreach ($e in $Exts) {
        $dst = Join-Path $ExtBase $e.Name
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue }
        # Junction mirrors the bash 'ln -s' (live link to source, no admin needed).
        New-Item -ItemType Junction -Path $dst -Target (Join-Path $Root $e.Src) | Out-Null
        Write-Host ("    {0} -> {1}" -f $e.Src, $dst)
    }

    Write-Host '==> Installing Markdown Preview Enhanced (sc code-block highlight in preview)'
    $code = Get-Command code -ErrorAction SilentlyContinue
    if (-not $code) { $code = Get-Command code.cmd -ErrorAction SilentlyContinue }
    if ($code) {
        cmd /c "`"$($code.Source)`" --install-extension shd101wyy.markdown-preview-enhanced --force" | Out-Null
        Write-Host '    ready (.crossnote/parser.js provides sc highlight; reload window to take effect)'
    }
    else {
        Write-Host "    SKIP: code CLI not found; install 'Markdown Preview Enhanced' manually in VSCode"
    }
    Write-Host '==> Install complete (restart VSCode for .sc/.ss highlighting and AST view)'
}

function Do-Uninstall {
    Write-Host '==> Uninstalling scc and VSCode extensions'
    $binScc = Join-Path (Join-Path $Prefix 'bin') 'scc.exe'
    if (Test-Path $binScc) { Remove-Item $binScc -Force -ErrorAction SilentlyContinue }
    foreach ($e in $Exts) {
        $dst = Join-Path $ExtBase $e.Name
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue }
    }
    Write-Host '==> Uninstall complete'
}

function Do-Clean {
    Remove-Item $BuildDir, $DistDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "==> Cleaned $BuildDir ($DistDir)"
}

switch ($Command) {
    'build'     { Do-Build | Out-Null }
    'dist'      { Do-Dist }
    'test'      { Do-Test $Arg2 }
    'install'   { Do-Install }
    'uninstall' { Do-Uninstall }
    'clean'     { Do-Clean }
    default     { Show-Usage }
}
