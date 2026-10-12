param([string]$BinaryDir, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$eventName = 'Local\WitnessSessionTest-' + [guid]::NewGuid().ToString('N')
$stop = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $eventName)
$skip = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-SkipMenu'))
$metrics = Join-Path $OutputDir ('session-host-' + [guid]::NewGuid().ToString('N') + '.json')
$fixture = Start-Process -FilePath (Join-Path $BinaryDir 'witness-render-test-host.exe') -ArgumentList $eventName -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
$fixtureHandle = $fixture.Handle
$loader = Join-Path $BinaryDir 'witness-probe-loader.exe'
$sessionLogs = @()
function Control([string]$Action) {
    $extra = @()
    if ($Action -eq 'start') {
        $log = Join-Path $OutputDir ('session-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $script:sessionLogs += $log
        $extra = @('--log', $log)
    }
    $output = & $loader --cursor-session $Action --test-host --pid $fixture.Id @extra
    if ($LASTEXITCODE -ne 0) { throw "Session command failed: $Action" }
    return ($output | ConvertFrom-Json)
}
try {
    $state = Control status
    if ($state.loaded -or $state.state -ne 'stopped') { throw 'Status unexpectedly injected a DLL' }
    $state = Control start
    if ($state.state -ne 'running' -or !$state.enabled) { throw 'Session did not start' }
    if ($state.show_hands -or $state.hands_fault -or $state.hand_draws -ne 0) { throw 'Hands must be disabled by default' }
    if ($state.world_occlusion -or $state.occlusion_active -or $state.occlusion_draws -ne 0) { throw 'World occlusion must be disabled by default' }
    # Prove that the session survives the old diagnostic's 30-second ceiling.
    Start-Sleep -Seconds 31
    $state = Control status
    if ($state.state -ne 'running' -or !$state.enabled -or $state.submitted -lt 100 -or $state.fault) { throw 'Session did not persist' }
    $duplicateLog = Join-Path $OutputDir ('duplicate-session-' + [guid]::NewGuid().ToString('N') + '.jsonl')
    $ErrorActionPreference = 'Continue'
    $duplicate = & $loader --cursor-session start --test-host --pid $fixture.Id --log $duplicateLog 2>&1
    $duplicateExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($duplicateExit -eq 0 -or (Test-Path -LiteralPath $duplicateLog)) { throw 'Duplicate start was not refused cleanly' }
    $traceLog = Join-Path $OutputDir ('blocked-trace-' + [guid]::NewGuid().ToString('N') + '.jsonl')
    $ErrorActionPreference = 'Continue'
    $blocked = & $loader --render-trace --test-host --pid $fixture.Id --log $traceLog --duration-ms 100 2>&1
    $blockedExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($blockedExit -eq 0) { throw 'Timed capture overlapped the session' }
    $state = Control status
    if ($state.state -ne 'running' -or !$state.enabled) { throw 'Refused trace damaged session' }
    $state = Control disable
    if ($state.enabled) { throw 'Disable failed' }
    Start-Sleep -Milliseconds 100
    $disabledCount = (Control status).submitted
    Start-Sleep -Milliseconds 100
    if ((Control status).submitted -ne $disabledCount) { throw 'Deferral continued while disabled' }
    $state = Control enable
    Start-Sleep -Milliseconds 100
    if ((Control status).submitted -le $disabledCount) { throw 'Deferral did not resume' }
    $state = Control stop
    if ($state.state -ne 'stopped' -or $state.enabled) { throw 'Stop failed' }
    if ($state.deferred -ne $state.submitted) { throw 'Completed menu submissions missing from session totals' }
    $state = Control start
    Start-Sleep -Milliseconds 100
    $skip.Set() | Out-Null
    Start-Sleep -Milliseconds 100
    $state = Control status
    if (!$state.fault -or $state.enabled -or $state.fallback -lt 1) { throw 'Skipped menu did not disable unsafe deferral' }
    $skip.Reset() | Out-Null
    $state = Control stop
    if ($state.state -ne 'stopped') { throw 'Faulted session could not stop' }
    $state = Control start
    Start-Sleep -Milliseconds 100
    $state = Control status
    if (!$state.enabled -or $state.fault) { throw 'Fresh session did not recover' }
    $state = Control stop
    foreach ($arguments in @(
        @('--cursor-session', 'status', '--hands'),
        @('--controller-session', 'start', '--no-log', '--hands'),
        @('--render-trace', '--hands'),
        @('--cursor-session', 'status', '--hand-occlusion'),
        @('--controller-session', 'start', '--no-log', '--hand-occlusion'),
        @('--render-trace', '--hand-occlusion')
    )) {
        $ErrorActionPreference = 'Continue'
        $invalid = & $loader @arguments --test-host --pid $fixture.Id 2>&1
        $invalidExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($invalidExit -eq 0) { throw 'Hand option was accepted outside render session start' }
    }
    $withoutLog = & $loader --cursor-session start --test-host --pid $fixture.Id --no-log --hands --hand-occlusion | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !$withoutLog.enabled -or $withoutLog.logging) { throw 'Session without logging failed' }
    if (!$withoutLog.show_hands) { throw 'Hands setting did not reach render session' }
    if (!$withoutLog.world_occlusion) { throw 'Occlusion setting did not reach render session' }
    Start-Sleep -Milliseconds 100
    $state = Control status
    if ($state.logging -or !$state.show_hands) { throw 'Logging or hands status changed' }
    if (!$state.world_occlusion) { throw 'Occlusion preference did not persist in session status' }
    if ($state.hands_fault -or $state.hand_draws -lt 2) { throw 'Enabled hands did not draw both eyes' }
    $state = Control disable
    if (!$state.show_hands -or !$state.world_occlusion -or $state.enabled) { throw 'Disabling fixes lost hand preferences' }
    Start-Sleep -Milliseconds 100
    $disabledHands = (Control status).hand_draws
    Start-Sleep -Milliseconds 100
    if ((Control status).hand_draws -ne $disabledHands) { throw 'Hands continued drawing while disabled' }
    $state = Control enable
    if (!$state.show_hands -or !$state.world_occlusion -or !$state.enabled) { throw 'Enabling fixes lost hand preferences' }
    Start-Sleep -Milliseconds 100
    if ((Control status).hand_draws -le $disabledHands) { throw 'Hands did not resume after enabling' }
    $state = Control stop
    $occlusionOnly = & $loader --cursor-session start --test-host --pid $fixture.Id --no-log --hand-occlusion | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or !$occlusionOnly.world_occlusion -or $occlusionOnly.show_hands -or $occlusionOnly.occlusion_active -or $occlusionOnly.occlusion_draws -ne 0) { throw 'Occlusion without visible hands did not remain inactive' }
    $state = Control stop
    $state = Control start
    if ($state.show_hands -or $state.hands_fault -or $state.hand_draws -ne 0) { throw 'Hands defaults were not restored on restart' }
    if ($state.world_occlusion -or $state.occlusion_active -or $state.occlusion_draws -ne 0) { throw 'Occlusion defaults were not restored on restart' }
    $state = Control stop
    foreach ($log in $sessionLogs) {
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        if ($events[0].event -ne 'session.started' -or $events[-1].event -ne 'session.stopped') { throw 'Session log lifecycle missing' }
        if ($events.Count -gt 20) { throw 'Session unexpectedly logged per-frame events' }
        if ($events[0].completion -ne 'menu' -or !$events[0].pause_redraw_enabled -or $events[-1].pause_redraw_enabled) { throw 'Session did not select or restore the tested pause behavior' }
    }
    $longSession = @(Get-Content -LiteralPath $sessionLogs[0] | ForEach-Object { $_ | ConvertFrom-Json })
    if (@($longSession | Where-Object { $_.event -eq 'session.heartbeat' -and $_.paused_scene_redraws -gt 100 }).Count -lt 1) { throw 'Pause redraw did not persist beyond the timed limit' }
    $fixture.Refresh()
    if ($fixture.HasExited) { throw 'Host did not survive session' }
    if (@($fixture.Modules | Where-Object ModuleName -eq 'witness-render-probe.dll').Count -ne 1) { throw 'Pinned DLL lifetime contract failed' }
} finally {
    $stop.Set() | Out-Null
    if (!$fixture.WaitForExit(5000)) { $fixture.Kill(); $fixture.WaitForExit() }
    $stop.Dispose()
    $skip.Dispose()
}
if (!$fixture.HasExited -or $fixture.ExitCode -ne 0) { throw 'Session host exit failed' }
$actual = Get-Content -LiteralPath $metrics | ConvertFrom-Json
if ($actual.before_cursor -lt 5 -or $actual.after_menu -lt 100 -or $actual.outside -lt 5) { throw 'Independent host submission counters failed' }
if ($actual.paused_redraws -lt 100 -or $actual.paused_skips -lt 5 -or $actual.pause_leaks -ne 0 -or $actual.unpaused_skips -ne 0) { throw 'Persistent pause override was missing, failed restoration, or affected other callers' }
if ($actual.hands_calls -lt 2 -or $actual.hands_bad_stage -ne 0 -or $actual.hands_bad_eye -ne 0) { throw 'Hands drew into the wrong eye or after the cursor' }
Write-Output 'Cursor/pause session: duration >30s, scoped pause redraw, hand draw routing/lifecycle, command controls, overlap refusal, skipped-menu fault, restart, bounded logs, and clean exit passed.'
