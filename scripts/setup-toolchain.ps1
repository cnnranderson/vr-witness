[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolsRoot = Join-Path $projectRoot '.tools'
$archive = Join-Path $toolsRoot 'downloads\w64devkit-x64-2.10.0.7z.exe'
$expectedHash = '18D0A4C71A166F8401AB6305781BEC5882B40B5E06BA9807C61CB5F3B3C6325E'
New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x64-2.10.0.7z.exe' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
    throw "Compiler archive checksum mismatch. Remove $archive and retry."
}
$compiler = Join-Path $toolsRoot 'w64devkit\bin\g++.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    # Windows bsdtar can read the archive without running its self-extractor.
    & tar.exe -xf $archive -C $toolsRoot
    if ($LASTEXITCODE -ne 0) { throw 'Compiler extraction failed' }
}
$target = & $compiler -dumpmachine
if ($LASTEXITCODE -ne 0 -or $target -ne 'x86_64-w64-mingw32') { throw 'Expected an x64 compiler' }
Write-Output "Portable compiler ready: $compiler"
