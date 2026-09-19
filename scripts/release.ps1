<#
.SYNOPSIS
Build, test and package one version in a fresh output directory.
.DESCRIPTION
Requires CMake 3.25+ and scripts/setup-toolchain.ps1. Refuses existing releases.
Records source hashes and tool versions; ZIPs are not guaranteed bit-identical.
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

# Hash only source inputs, never local configuration, logs, game files or tools.
function Get-SourceManifest {
    $files = @(Get-Item -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt'))
    foreach ($directory in '.github','src','tests','scripts','packaging','third_party','assets') {
        $files += Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -File -Recurse |
            Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' }
    }
    ($files | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($projectRoot.Length + 1).Replace('\','/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    }) -join "`n"
}
$buildName = 'release-' + $Version.Replace('.','-') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$buildRoot = Join-Path $projectRoot "out\$buildName"
New-Item -ItemType Directory -Path $buildRoot | Out-Null
$sourceManifest = Get-SourceManifest
$utf8 = [Text.UTF8Encoding]::new($false)
$sourceFile = Join-Path $buildRoot 'source-hashes.txt'
[IO.File]::WriteAllText($sourceFile, $sourceManifest + "`n", $utf8)
$previousReleasePath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $cmake) + ';' + $env:PATH
    Write-Output "Building $Version in $buildName"
    # Hosted Windows runners have less capacity for concurrent process injection tests.
    $testJobs = if ($env:GITHUB_ACTIONS -eq 'true') { 1 } else { 4 }
    & (Join-Path $PSScriptRoot 'build.ps1') -BuildName $buildName -Version $Version -Configuration Release -Test -TestSuite All -TestJobs $testJobs
    if ((Get-SourceManifest) -cne $sourceManifest) {
        throw 'Source changed during the release build. No package was created; rerun after editing finishes.'
    }
    $compilerVersion = & $compiler -dumpfullversion
    if ($LASTEXITCODE -ne 0) { throw 'Could not read compiler version.' }
    $cmakeVersion = (& $cmake --version | Select-Object -First 1)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read CMake version.' }
    $testJson = & (Join-Path (Split-Path -Parent $cmake) 'ctest.exe') --test-dir $buildRoot --show-only=json-v1
    if ($LASTEXITCODE -ne 0) { throw 'Could not list release tests.' }
    $testNames = @(($testJson -join "`n" | ConvertFrom-Json).tests | ForEach-Object { $_.name })
    $binaries = [ordered]@{}
    foreach ($name in 'WitnessVR.exe','witness-probe-loader.exe','witness-input-probe.dll','witness-render-probe.dll') {
        $binaries[$name] = (Get-FileHash -LiteralPath (Join-Path $buildRoot "bin\$name") -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $info = [ordered]@{
        version = $Version
        configuration = 'Release'
        architecture = 'windows-x64'
        builtUtc = [DateTime]::UtcNow.ToString('o')
        compiler = "GCC $compilerVersion (w64devkit)"
        cmake = $cmakeVersion
        compilerSha256 = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant()
        testedBinariesSha256 = $binaries
        sourceManifestSha256 = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash.ToLowerInvariant()
        tests = [ordered]@{ result = 'passed'; count = $testNames.Count; names = $testNames }
    }
    [IO.File]::WriteAllText((Join-Path $buildRoot 'build-info.json'), ($info | ConvertTo-Json -Depth 4) + "`n", $utf8)
    & (Join-Path $PSScriptRoot 'package.ps1') -BuildName $buildName -Version $Version
} finally {
    $env:PATH = $previousReleasePath
}
