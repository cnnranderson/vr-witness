param([string]$BinaryDir, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$eventName = 'Local\WitnessInputTest-' + [guid]::NewGuid().ToString('N')
$stop = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $eventName)
$legacy = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Legacy'))
$aim = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Aim'))
$metrics = Join-Path $OutputDir ('input-host-' + [guid]::NewGuid().ToString('N') + '.json')
$fixture = Start-Process -FilePath (Join-Path $BinaryDir 'witness-input-test-host.exe') -ArgumentList $eventName -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
$fixtureHandle = $fixture.Handle
try {
    foreach ($mode in @('trace', 'move', 'legacy_rejected', 'legacy_move', 'aim_trace', 'aim', 'aim_trace', 'trace')) {
        $log = Join-Path $OutputDir ('input-' + $mode + '-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        if ($mode -like 'aim*') { $aim.Set() | Out-Null } else { $aim.Reset() | Out-Null }
        $extra = @(); if ($mode -in @('move','legacy_rejected','legacy_move')) { $extra += '--controller-move' }
        if ($mode -like 'legacy*') { $legacy.Set() | Out-Null } else { $legacy.Reset() | Out-Null }
        if ($mode -eq 'aim') { $extra += '--controller-aim' }
        if ($mode -eq 'aim_trace') { $extra += '--controller-aim-trace' }
        if ($mode -eq 'legacy_move') { $extra += '--controller-legacy-axis0' }
        & (Join-Path $BinaryDir 'witness-probe-loader.exe') --controller-trace --test-host @extra --pid $fixture.Id --log $log --duration-ms 3000
        if ($LASTEXITCODE -ne 0) { throw 'Input capture failed' }
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        $last = $events[-1]
        if ($last.event -ne 'input.finished' -or !$last.hooks_disabled -or !$last.dll_pinned_until_exit -or $last.polls -lt 50 -or $last.movement_calls -lt 50) { throw 'Input hook/lifetime contract failed' }
        if ($mode -in @('move','legacy_move')) {
            if ($last.applied -lt 5) { throw 'No movement applied' }
        } elseif ($last.applied -ne 0) { throw 'Trace mode applied movement' }
        if ($mode -like 'aim*' -and ($last.cursor_calls -lt 50 -or $last.pose_samples -lt 20)) { throw 'No cursor/pose observations' }
        if ($mode -eq 'aim') { if ($last.aim_applied -lt 5) { throw 'No pointing applied' } } elseif ($last.aim_applied -ne 0) { throw 'Passive capture applied pointing' }
        $fixture.Refresh(); if ($fixture.HasExited) { throw 'Input host did not survive' }
    }
} finally {
    $stop.Set() | Out-Null
    if (!$fixture.WaitForExit(5000)) { $fixture.Kill(); $fixture.WaitForExit() }
    $stop.Dispose(); $legacy.Dispose(); $aim.Dispose()
}
if ($fixture.ExitCode -ne 0) { throw "Input host exit $($fixture.ExitCode)" }
$actual = Get-Content -LiteralPath $metrics | ConvertFrom-Json
if ($actual.moved -lt 5 -or $actual.right -lt 5 -or $actual.neutral -lt 50 -or $actual.leaks -ne 0 -or $actual.decoy_leaks -ne 0 -or $actual.aim_changed -lt 5 -or $actual.aim_leaks -ne 0 -or $actual.aim_decoy_leaks -ne 0) { throw "Input fixture behavior failed: $(Get-Content -LiteralPath $metrics)" }
Write-Output 'Controller role/axis ABI, movement, mode/focus/pause/disconnect/failed-poll gates, neutral rearm, caller isolation, trace-only behavior, repeated teardown and host survival passed.'
