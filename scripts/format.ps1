<#
.SYNOPSIS
Format project-owned C++ and Python, or check formatting without changing files.
.DESCRIPTION
Install the optional tools from config/formatters.txt into .tools/formatters first.
#>
[CmdletBinding()]
param([switch]$Check)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$formatterRoot = Join-Path $projectRoot '.tools\formatters'
$clangFormat = Join-Path $formatterRoot 'clang_format\data\bin\clang-format.exe'
if (!(Test-Path -LiteralPath $clangFormat)) {
    throw 'Install tools: python -m pip install --target .tools/formatters -r config/formatters.txt'
}
$cppFiles = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src'), (Join-Path $projectRoot 'tests') -File -Recurse |
    Where-Object { $_.Extension -in '.cpp', '.hpp' } | ForEach-Object { $_.FullName })
[string[]]$clangArgs = if ($Check) { @('--dry-run', '--Werror') } else { @('-i') }
& $clangFormat @clangArgs @cppFiles
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting check failed.' }
$previousPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = $formatterRoot
    if ($previousPythonPath) { $env:PYTHONPATH += ';' + $previousPythonPath }
    $blackArgs = @('-m', 'black', '--quiet', '--config', (Join-Path $projectRoot 'pyproject.toml'))
    if ($Check) { $blackArgs += '--check' }
    & python @blackArgs (Join-Path $projectRoot 'tools') (Join-Path $projectRoot 'tests')
    if ($LASTEXITCODE -ne 0) { throw 'Python formatting check failed.' }
} finally {
    $env:PYTHONPATH = $previousPythonPath
}
Write-Output $(if ($Check) { 'Formatting checks passed.' } else { 'Source formatting complete.' })
