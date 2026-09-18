[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BinaryDir, [Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference = 'Stop'
$BinaryDir = (Resolve-Path -LiteralPath $BinaryDir).Path
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$fixture = Join-Path $BinaryDir 'witness-probe-test-host.exe'
$loader = Join-Path $BinaryDir 'witness-probe-loader.exe'
$session = [guid]::NewGuid().ToString('N')
$eventName = 'Local\WitnessVrProbeTest-' + $session
$stopEvent = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $eventName)
$hostProcess = $null
try {
    $hostProcess = Start-Process -FilePath $fixture -ArgumentList $eventName -WindowStyle Hidden -PassThru
    # Give the fixture a brief startup window before querying its image path.
    Start-Sleep -Milliseconds 250
    if ($hostProcess.HasExited) { throw 'Fixture exited before injection' }
    $log = Join-Path $OutputDir ("smoke-$session.jsonl")

    # A wrong expected game path MUST be rejected before injection or log I/O.
    $ErrorActionPreference = 'Continue'
    try {
        & $loader --pid $hostProcess.Id --game-exe (Join-Path $BinaryDir 'witness64_d3d11.exe') --log $log --duration-ms 0 2>&1 | ForEach-Object { Write-Output "Negative test: $_" }
    } finally {
        $ErrorActionPreference = 'Stop'
    }
    if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $log)) { throw 'Wrong-target check failed' }

    # Two successful attaches to the same live process prove the DLL unloads,
    # and exercise real cross-process execution rather than a local-only test.
    foreach ($round in 1,2) {
        $roundLog = Join-Path $OutputDir ("smoke-$session-$round.jsonl")
        & $loader --pid $hostProcess.Id --test-host --log $roundLog --duration-ms 200
        if ($LASTEXITCODE -ne 0) { throw "Injection round $round failed" }
        $events = @(Get-Content -LiteralPath $roundLog | ForEach-Object { $_ | ConvertFrom-Json })
        if ($events[0].event -ne 'probe.started' -or $events[-1].event -ne 'probe.finished') { throw 'Missing start/finish log events' }
        if (@($events | Where-Object event -eq 'module.loaded').Count -eq 0) { throw 'No module inventory captured' }
        if (@($events | Where-Object event -eq 'runtime.snapshot').Count -ne 6) { throw 'Missing runtime snapshots' }
        $caps = @($events | Where-Object event -eq 'probe.capabilities')
        if ($caps.Count -ne 1 -or $caps[0].camera_writes -or $caps[0].render_hooks -or $caps[0].vr_initialized_by_probe) { throw 'Unexpected probe capabilities' }
        if (@($events | Where-Object pid -ne $hostProcess.Id).Count) { throw 'Probe ran in wrong process' }
        $hostProcess.Refresh()
        if ($hostProcess.HasExited) { throw 'Fixture crashed' }
        if (@($hostProcess.Modules | Where-Object ModuleName -eq 'witness-probe.dll').Count) { throw 'DLL still loaded after completion' }
    }
    Write-Output 'PASS: wrong-target rejection, x64 injection, JSONL inventory, repeat attach, clean unload, target survival.'
} finally {
    $stopEvent.Set() | Out-Null
    if ($hostProcess -and -not $hostProcess.WaitForExit(3000)) {
        # Only the fixture process created by this test is eligible for cleanup.
        $hostProcess.Kill()
        $hostProcess.WaitForExit()
    }
    $stopEvent.Dispose()
    if ($hostProcess) { $hostProcess.Dispose() }
}
