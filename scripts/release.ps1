<#
.SYNOPSIS
Build, test and package one version in a fresh output directory.
.DESCRIPTION
Requires CMake 3.25+ and scripts/setup-toolchain.ps1. Refuses existing releases.
Records tool versions and test results; ZIPs are not guaranteed bit-identical.
.EXAMPLE
.\scripts\release.ps1 -Version 0.1.0-preview.2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9][a-zA-Z0-9.-]*)?$')][string]$Version
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$releaseBase = Join-Path $projectRoot "out\releases\WitnessVR-$Version"
if ((Test-Path -LiteralPath $releaseBase) -or (Test-Path -LiteralPath "$releaseBase.zip")) {
    throw 'This version already exists. Choose a new version; published packages are not overwritten.'
}
$compiler = Join-Path $projectRoot '.tools\w64devkit\bin\g++.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'First run .\scripts\setup-toolchain.ps1.' }
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe' }
if (!(Test-Path -LiteralPath $cmake)) { throw 'Install CMake 3.25 or newer, then retry.' }

$buildName = 'release-' + $Version.Replace('.','-') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$buildRoot = Join-Path $projectRoot "out\$buildName"
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$previousReleasePath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $cmake) + ';' + $env:PATH
    Write-Output "Building $Version in $buildName"
    # Hosted Windows runners have less capacity for concurrent process injection tests.
    $testJobs = if ($env:GITHUB_ACTIONS -eq 'true') { 1 } else { 4 }
    & (Join-Path $PSScriptRoot 'build.ps1') -BuildName $buildName -Version $Version -Configuration Release -Test -TestSuite All -TestJobs $testJobs
    $compilerVersion = & $compiler -dumpfullversion
    if ($LASTEXITCODE -ne 0) { throw 'Could not read compiler version.' }
    $cmakeVersion = (& $cmake --version | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read CMake version.' }
    $testJson = & (Join-Path (Split-Path -Parent $cmake) 'ctest.exe') --test-dir $buildRoot --show-only=json-v1
    if ($LASTEXITCODE -ne 0) { throw 'Could not list release tests.' }
    $testNames = @(($testJson -join "`n" | ConvertFrom-Json).tests | ForEach-Object { $_.name })
    $info = [ordered]@{
        version = $Version
        configuration = 'Release'
        architecture = 'windows-x64'
        builtUtc = [DateTime]::UtcNow.ToString('o')
        compiler = "GCC $compilerVersion (w64devkit)"
        cmake = $cmakeVersion
        tests = [ordered]@{ result = 'passed'; count = $testNames.Count; names = $testNames }
    }
    [IO.File]::WriteAllText((Join-Path $buildRoot 'build-info.json'), ($info | ConvertTo-Json -Depth 4) + "`n", $utf8)
    & (Join-Path $PSScriptRoot 'package.ps1') -BuildName $buildName -Version $Version
} finally {
    $env:PATH = $previousReleasePath
}
