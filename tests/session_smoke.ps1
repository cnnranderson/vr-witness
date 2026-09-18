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
Write-Output 'Cursor/pause session: duration >30s, scoped pause redraw, command controls, overlap refusal, skipped-menu fault, restart, bounded logs, and clean exit passed.'
