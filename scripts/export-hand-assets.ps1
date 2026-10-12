<#
.SYNOPSIS
Export and validate the supplied glove scenes without starting XR or the game.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$GodotExe)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$engine = (Resolve-Path -LiteralPath $GodotExe).Path
$version = (& $engine --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $version -notlike '4.7.1.stable.*') {
    throw 'This asset exporter requires Godot 4.7.1 stable.'
}
$scratch = Join-Path $projectRoot ('out\hand-export-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $scratch | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $scratch 'project.godot'), "config_version=5`n", $utf8)
$source = Join-Path $projectRoot 'assets\hands\source'
$output = Join-Path $scratch 'exported'
$script = Join-Path $projectRoot 'tools\export_glove_hands.gd'
& $engine --headless --xr-mode off --quit-after 30 --log-file (Join-Path $scratch 'export.log') `
    --path $scratch --script $script -- $source $output
if ($LASTEXITCODE -ne 0) { throw "Hand export failed; see $scratch" }
$reportPath = Join-Path $output 'export-report.json'
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($report.hands.Count -ne 2 -or @($report.hands | Where-Object { !$_.round_trip_validated }).Count) {
    throw 'Both hands must pass round-trip validation before copying.'
}
foreach ($hand in $report.hands) {
    if ($hand.side -notin 'left','right' -or $hand.triangles -ne 456 -or $hand.bones -ne 26) {
        throw 'Unexpected hand geometry or skeleton; review the source asset.'
    }
    $file = Join-Path $output ($hand.side + '_hand.glb')
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ine $hand.glb_sha256) {
        throw 'Exported asset hash does not match the validation report.'
    }
}
$destination = Join-Path $projectRoot 'assets\hands'
Copy-Item -LiteralPath (Join-Path $output 'left_hand.glb'),(Join-Path $output 'right_hand.glb'),$reportPath -Destination $destination
Write-Output "Validated glove assets: $destination"
Write-Output "Export log: $scratch"
