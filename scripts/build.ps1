[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [switch]$Test,
    [switch]$Fresh,
    [ValidateSet('All','Math','Input','Snap','Render','Lifecycle')][string]$TestSuite = 'All',
    [ValidateRange(1,4)][int]$TestJobs = 4,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildName = 'build'
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
# GNU Make's recursive invocation uses its program name. Scope PATH to this
# script and restore it below; never change the user's persisted environment.
$env:PATH = $toolBin + ';' + $env:PATH
$env:Path = $env:PATH
& $cmake @configureOptions -S $projectRoot -B $buildRoot -G 'MinGW Makefiles' "-DCMAKE_C_COMPILER=$((Join-Path $toolBin 'gcc.exe').Replace('\','/'))" "-DCMAKE_CXX_COMPILER=$($compiler.Replace('\','/'))" "-DCMAKE_MAKE_PROGRAM=$($make.Replace('\','/'))" "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmake --build $buildRoot --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Test) {
    $testArgs = @('--test-dir',$buildRoot,'--output-on-failure','--parallel',$TestJobs)
    $patterns = @{
        Math = '^controller_math$'
        Input = '^(controller_math|controller_injection_smoke|stick_cursor_smoke)$'
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
