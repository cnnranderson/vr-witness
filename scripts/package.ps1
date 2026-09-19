<#
.SYNOPSIS
Package an existing tested Release build as a portable folder and ZIP.
.DESCRIPTION
Normally called by release.ps1. Uses an explicit file allowlist; excludes personal settings, logs, tools and game files.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildName,
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9._-]+$')][string]$Version
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "out\$BuildName"
$binaryDir = Join-Path $buildRoot 'bin'
$recordDir = Join-Path $projectRoot "out\release-records\WitnessVR-$Version"
$buildInfo = Join-Path $buildRoot 'build-info.json'
$sourceHashes = Join-Path $buildRoot 'source-hashes.txt'
$releaseRoot = Join-Path $projectRoot 'out\releases'
$packageDir = Join-Path $releaseRoot "WitnessVR-$Version"
$archive = "$packageDir.zip"
if ((Test-Path -LiteralPath $packageDir) -or (Test-Path -LiteralPath $archive) -or (Test-Path -LiteralPath $recordDir)) {
    throw 'This version already exists. Use a new Version to preserve existing packages.'
}
$strip = Join-Path $projectRoot '.tools\w64devkit\bin\strip.exe'
$entries = @{
    'WitnessVR.exe' = (Join-Path $binaryDir 'WitnessVR.exe')
    'runtime\witness-probe-loader.exe' = (Join-Path $binaryDir 'witness-probe-loader.exe')
    'runtime\witness-input-probe.dll' = (Join-Path $binaryDir 'witness-input-probe.dll')
    'runtime\witness-render-probe.dll' = (Join-Path $binaryDir 'witness-render-probe.dll')
    'README.txt' = (Join-Path $projectRoot 'packaging\README.txt')
    'config\input.ini' = (Join-Path $projectRoot 'packaging\input.ini')
    'licenses\MinHook-LICENSE.txt' = (Join-Path $projectRoot 'third_party\minhook\LICENSE.txt')
    'licenses\MinHook-PROVENANCE.md' = (Join-Path $projectRoot 'third_party\minhook\PROVENANCE.md')
    'licenses\GCC-COPYING3.txt' = (Join-Path $projectRoot 'third_party\gcc-runtime\COPYING3.txt')
    'licenses\GCC-COPYING.RUNTIME.txt' = (Join-Path $projectRoot 'third_party\gcc-runtime\COPYING.RUNTIME.txt')
    'licenses\MinGW-w64-runtime.txt' = (Join-Path $projectRoot '.tools\w64devkit\COPYING.MinGW-w64-runtime.txt')
    'licenses\THIRD-PARTY-NOTICES.txt' = (Join-Path $projectRoot 'packaging\THIRD-PARTY-NOTICES.txt')
}
foreach ($source in @($entries.Values) + @($strip,$buildInfo,$sourceHashes)) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing package input: $source" }
}
$info = Get-Content -LiteralPath $buildInfo -Raw | ConvertFrom-Json
if ($info.version -cne $Version -or $info.configuration -ne 'Release' -or $info.tests.result -ne 'passed') {
    throw 'Release metadata does not match. Use scripts/release.ps1 to build and validate this version.'
}
$binaryVersion = (Get-Item -LiteralPath $entries['WitnessVR.exe']).VersionInfo.ProductVersion
if ($binaryVersion -cne $Version) { throw "Launcher version $binaryVersion does not match package $Version." }
foreach ($property in $info.testedBinariesSha256.PSObject.Properties) {
    $binary = Join-Path $binaryDir $property.Name
    if ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ine $property.Value) {
        throw "Binary changed after testing: $($property.Name). Run release.ps1 again."
    }
}
foreach ($entry in $entries.GetEnumerator()) {
    $target = Join-Path $packageDir $entry.Key
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $entry.Value -Destination $target
    if ([IO.Path]::GetExtension($target) -in '.exe','.dll') {
        & $strip --strip-unneeded $target
        if ($LASTEXITCODE -ne 0) { throw "Could not strip package binary: $target" }
    }
}
$readme = Join-Path $packageDir 'README.txt'
[IO.File]::WriteAllText($readme, ([IO.File]::ReadAllText($readme).Replace('@VERSION@', $Version)), [Text.UTF8Encoding]::new($false))
$manifest = foreach ($file in (Get-ChildItem -LiteralPath $packageDir -File -Recurse | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($packageDir.Length + 1).Replace('\','/')
    '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
New-Item -ItemType Directory -Path $recordDir | Out-Null
Copy-Item -LiteralPath $buildInfo,$sourceHashes -Destination $recordDir
[IO.File]::WriteAllLines((Join-Path $recordDir 'SHA256SUMS.txt'), $manifest, [Text.UTF8Encoding]::new($false))
$testLog = Join-Path $buildRoot 'Testing\Temporary\LastTest.log'
if (Test-Path -LiteralPath $testLog) { Copy-Item -LiteralPath $testLog -Destination (Join-Path $recordDir 'tests.log') }
Compress-Archive -LiteralPath $packageDir -DestinationPath $archive -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$zipHash  $([IO.Path]::GetFileName($archive))`n", [Text.UTF8Encoding]::new($false))
$latest = [ordered]@{ version = $Version; buildName = $BuildName }
[IO.File]::WriteAllText((Join-Path (Split-Path -Parent $recordDir) 'latest.json'), ($latest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
Write-Output "Build records (local only): $recordDir"
Write-Output "Portable folder: $packageDir"
Write-Output "Share this ZIP: $archive"
