param([string]$BinaryDir, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$eventName = 'Local\WitnessInputSessionTest-' + [guid]::NewGuid().ToString('N')
$stop = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $eventName)
$legacy = New-Object System.Threading.EventWaitHandle($true, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Legacy'))
$held = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Held'))
$neutral = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Neutral'))
$aim = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Aim'))
$recenter = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-Recenter'))
$metrics = Join-Path $OutputDir ('input-session-host-' + [guid]::NewGuid().ToString('N') + '.json')
$fixture = Start-Process -FilePath (Join-Path $BinaryDir 'witness-input-test-host.exe') -ArgumentList $eventName -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
$fixtureHandle = $fixture.Handle
$loader = Join-Path $BinaryDir 'witness-probe-loader.exe'
$sessionLogs = @()
function Control([string]$Action, [bool]$Pointing = $true) {
    $extra = @()
    if ($Action -eq 'start') {
        $log = Join-Path $OutputDir ('input-session-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $script:sessionLogs += $log
        $extra = @('--log', $log, '--controller-legacy-axis0')
        if ($Pointing) { $extra += @('--controller-aim','--aim-speed-percent','60','--aim-smoothing-ms','100') }
    }
    $output = & $loader --controller-session $Action --test-host --pid $fixture.Id @extra
    if ($LASTEXITCODE -ne 0) { throw "Input session command failed: $Action" }
    return ($output | ConvertFrom-Json)
}
try {
    $state = Control status
    if ($state.loaded -or $state.state -ne 'stopped') { throw 'Status injected unexpectedly' }
    $state = Control start
    if ($state.state -ne 'running' -or !$state.enabled -or !$state.legacy_axis0) { throw 'Input session did not start' }
    if (!$state.pointing -or !$state.aim_enabled -or $state.aim_speed_percent -ne 60 -or $state.aim_smoothing_ms -ne 100) { throw 'Pointing settings missing' }
    Start-Sleep -Seconds 2
    $aim.Set() | Out-Null
    Start-Sleep -Seconds 31
    $state = Control status
    if ($state.state -ne 'running' -or !$state.enabled -or $state.applied -lt 5 -or $state.aim_applied -lt 100 -or $state.fault) { throw 'Input did not survive 30 seconds' }
    $observeLog=Join-Path $OutputDir ('observe-session-'+[guid]::NewGuid().ToString('N')+'.jsonl')
    & $loader --controller-trace --test-host --pid $fixture.Id --log $observeLog --duration-ms 200 | Out-Null
    if($LASTEXITCODE -ne 0){throw 'Live passive observation failed'}
    $observed=@(Get-Content -LiteralPath $observeLog | ForEach-Object {$_|ConvertFrom-Json})
    $observedState=Control status
    if($observed[0].event -ne 'input.observation.started' -or $observed[-1].event -ne 'input.observation.finished' -or
       !$observed[-1].session_running -or !$observedState.aim_enabled -or $observedState.fault -or $observedState.aim_speed_percent -ne 60 -or
       @($observed|Where-Object event -eq 'input.sample').Count -lt 3){throw 'Observer interrupted or reconfigured session'}
    $aim.Reset() | Out-Null
    foreach ($mode in @('start','trace')) {
        $log = Join-Path $OutputDir ('refused-input-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $ErrorActionPreference = 'Continue'
        if ($mode -eq 'start') { $rejected = & $loader --controller-session start --test-host --pid $fixture.Id --log $log 2>&1 }
        else { $rejected = & $loader --controller-trace --controller-move --test-host --pid $fixture.Id --log $log --duration-ms 100 2>&1 }
        $rejectedExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($rejectedExit -eq 0 -or (Test-Path -LiteralPath $log)) { throw 'Overlap not refused before opening a log' }
    }
    $state = Control disable
    if ($state.enabled) { throw 'Input disable failed' }
    $held.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $disabledCount = (Control status).applied
    Start-Sleep -Milliseconds 150
    if ((Control status).applied -ne $disabledCount) { throw 'Movement continued disabled' }
    $state = Control enable
    Start-Sleep -Milliseconds 150
    if ((Control status).applied -ne $disabledCount) { throw 'Held stick rearmed without neutral' }
    $neutral.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $neutral.Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).applied -le $disabledCount) { throw 'Neutral did not rearm input' }
    # A held drawing trigger cannot rearm pointing after a session toggle.
    $state = Control disable
    $aim.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $disabledAim = (Control status).aim_applied
    Start-Sleep -Milliseconds 150
    if ((Control status).aim_applied -ne $disabledAim) { throw 'Pointing continued disabled' }
    $state = Control enable
    Start-Sleep -Milliseconds 150
    if ((Control status).aim_applied -ne $disabledAim) { throw 'Held trigger rearmed pointing' }
    $neutral.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).aim_applied -le $disabledAim) { throw 'Trigger release failed to rearm pointing' }
    $beforeRecenter = (Control status).recenter_count
    $recenter.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).recenter_count -ne ($beforeRecenter + 1)) { throw 'A did not recenter exactly once' }
    Start-Sleep -Milliseconds 150
    if ((Control status).recenter_count -ne ($beforeRecenter + 1)) { throw 'Held A repeated recenter' }
    $recenter.Reset() | Out-Null
    $legacy.Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    $recenter.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).recenter_count -ne ($beforeRecenter + 2)) { throw 'Standard-layout A did not recenter' }
    $recenter.Reset() | Out-Null
    $legacy.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $neutral.Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    $recenter.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).recenter_count -ne ($beforeRecenter + 2)) { throw 'A recentered with the draw trigger held' }
    $recenter.Reset() | Out-Null
    $neutral.Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if (!(Control status).aim_enabled) { throw 'Configured pointing was lost' }
    $state = Control stop
    if ($state.state -ne 'stopped' -or $state.enabled) { throw 'Input stop failed' }
    $stoppedCount = $state.applied
    $stoppedPolls = $state.polls
    $stoppedAim = $state.aim_applied
    $stoppedCursor = $state.cursor_calls
    Start-Sleep -Milliseconds 150
    $state = Control status
    if ($state.applied -ne $stoppedCount -or $state.polls -ne $stoppedPolls -or $state.aim_applied -ne $stoppedAim -or $state.cursor_calls -ne $stoppedCursor) { throw 'Input callbacks continued after stop' }
    $aim.Reset() | Out-Null
    $state = Control start $false
    if ($state.pointing -or $state.aim_enabled) { throw 'Walking-only restart unexpectedly enables pointing' }
    Start-Sleep -Milliseconds 150
    if ((Control status).applied -ne 0) { throw 'Restart accepted held stick' }
    $neutral.Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $neutral.Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).applied -lt 1) { throw 'Restart failed after neutral' }
    $state = Control stop
    $withoutLog = & $loader --controller-session start --test-host --pid $fixture.Id --no-log | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !$withoutLog.enabled -or $withoutLog.logging) { throw 'Session without logging failed' }
    Start-Sleep -Milliseconds 100
    if ((Control status).logging) { throw 'Disabled logging status changed' }
    $state = Control stop
    foreach ($log in $sessionLogs) {
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        if ($events[0].event -ne 'input.session.started' -or $events[-1].event -ne 'input.session.stopped') { throw 'Input session log lifecycle failed' }
        if ($events.Count -gt 20 -or $events[-1].enabled) { throw 'Input log volume/state failed' }
    }
    $events = @(Get-Content -LiteralPath $sessionLogs[0] | ForEach-Object { $_ | ConvertFrom-Json })
    if (@($events | Where-Object event -eq 'input.session.heartbeat').Count -lt 1) { throw 'Input heartbeat missing' }
    $fixture.Refresh(); if ($fixture.HasExited) { throw 'Input session host exited early' }
} finally {
    $stop.Set() | Out-Null
    if (!$fixture.WaitForExit(5000)) { $fixture.Kill(); $fixture.WaitForExit() }
    $stop.Dispose(); $legacy.Dispose(); $held.Dispose(); $neutral.Dispose(); $aim.Dispose(); $recenter.Dispose()
}
if ($fixture.ExitCode -ne 0) { throw 'Input session host failed' }
$actual = Get-Content -LiteralPath $metrics | ConvertFrom-Json
if ($actual.moved -lt 5 -or $actual.aim_changed -lt 100 -or $actual.aim_leaks -ne 0 -or $actual.aim_decoy_leaks -ne 0 -or $actual.leaks -ne 0 -or $actual.decoy_leaks -ne 0) { throw 'Independent input counters failed' }
Write-Output 'Persistent input: >30s, command controls, held-stick/trigger rearm, pointing persistence and custom settings, A-button edge/drawing gates, overlap rejection, restart, bounded logging, hook restoration and host survival passed.'
