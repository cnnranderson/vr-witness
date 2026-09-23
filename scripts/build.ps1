<#
.SYNOPSIS
Build a named output directory and optionally run an affected test group.
.DESCRIPTION
Never rebuild a DLL loaded in the game; use a new BuildName or close the game first.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [switch]$Test,
    [switch]$UiSmokeTest,
    [switch]$Fresh,
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9][a-zA-Z0-9.-]*)?$')][string]$Version = '0.1.0-dev',
    [ValidateSet('All','Math','Input','Snap','Render','Lifecycle','Launcher')][string]$TestSuite = 'All',
    [ValidateRange(1,4)][int]$TestJobs = 4,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildName = 'dev'
)
$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.ContainsKey('TestSuite') -and !$Test) { throw 'TestSuite requires -Test.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolBin = Join-Path $projectRoot '.tools\w64devkit\bin'
$compiler = Join-Path $toolBin 'g++.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw 'Run .\scripts\setup-toolchain.ps1 first. It installs only inside this project.'
}
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$make = Join-Path $toolBin 'mingw32-make.exe'
$buildRoot = Join-Path (Join-Path $projectRoot 'out') $BuildName
$configureOptions = @()
if ($Fresh) { $configureOptions += '--fresh' }
$previousBuildPath = $env:PATH
$previousMixedCaseBuildPath = $env:Path
try {
    # Keep GNU Make on PATH only for this build process.
    $env:PATH = $toolBin + ';' + $env:PATH
    $env:Path = $env:PATH
    & $cmake @configureOptions -S $projectRoot -B $buildRoot -G 'MinGW Makefiles' "-DCMAKE_C_COMPILER=$((Join-Path $toolBin 'gcc.exe').Replace('\','/'))" "-DCMAKE_CXX_COMPILER=$($compiler.Replace('\','/'))" "-DCMAKE_MAKE_PROGRAM=$($make.Replace('\','/'))" "-DCMAKE_BUILD_TYPE=$Configuration" "-DWITNESS_RELEASE_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & $cmake --build $buildRoot --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($UiSmokeTest) {
        if (Get-Process -Name witness64_d3d11 -ErrorAction SilentlyContinue) {
            throw 'Close The Witness before running the settings/UI smoke test.'
        }
        & $cmake --build $buildRoot --target witness-launcher-ui-test --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'UI test build failed' }
        $uiOutput = Join-Path $buildRoot ('ui-smoke-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
        New-Item -ItemType Directory -Path $uiOutput | Out-Null
        $uiProcess = Start-Process -FilePath (Join-Path $buildRoot 'bin\witness-launcher-ui-test.exe') -ArgumentList ('"' + $uiOutput + '"') -WindowStyle Hidden -PassThru
        if (!$uiProcess.WaitForExit(20000)) {
            Stop-Process -InputObject $uiProcess -Force
            throw "UI test timed out. Inspect $uiOutput."
        }
        if ($uiProcess.ExitCode -ne 0) { throw "UI test failed. Inspect $uiOutput." }
        foreach ($capture in 'status.bmp','settings.bmp','ui-check.txt') {
            if (!(Test-Path -LiteralPath (Join-Path $uiOutput $capture))) {
                throw "UI test did not produce $capture. Inspect $uiOutput."
            }
        }
        Write-Output "UI checks passed; captures in $uiOutput"
    }
    if ($Test) {
        $testArgs = @('--test-dir',$buildRoot,'--output-on-failure','--parallel',$TestJobs)
        $patterns = @{
            Launcher = '^launcher_backend$'
            Math = '^controller_math$'
            Input = '^(controller_math|resting_height_hook|controller_injection_smoke|stick_cursor_smoke)$'
            Snap = '^(controller_math|snap_turn_smoke)$'
            Render = '^render_injection_smoke$'
            Lifecycle = '^(cursor_session_smoke|controller_session_smoke)$'
        }
        if ($TestSuite -ne 'All') { $testArgs += @('-R',$patterns[$TestSuite]) }
        & $ctest @testArgs
        if ($LASTEXITCODE -ne 0) { throw 'Smoke test failed' }
    }
    Write-Output "Built in $buildRoot\bin"
} finally {
    $env:PATH = $previousBuildPath
    $env:Path = $previousMixedCaseBuildPath
}
