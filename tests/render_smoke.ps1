param([string]$BinaryDir, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$eventName = 'Local\WitnessRenderTest-' + [guid]::NewGuid().ToString('N')
$stop = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, $eventName)
$skipMenu = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + '-SkipMenu'))
$metrics = Join-Path $OutputDir ('render-host-' + [guid]::NewGuid().ToString('N') + '.json')
$fixture = Start-Process -FilePath (Join-Path $BinaryDir 'witness-render-test-host.exe') -ArgumentList $eventName -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
# Keep a process handle so Windows PowerShell can retrieve the exit status
# after the short-lived process has disappeared from the process table.
$fixtureHandle = $fixture.Handle
try {
    foreach ($mode in @('trace', 'defer', 'menu', 'menu_fault', 'pause', 'pause_fault', 'pause', 'menu', 'defer', 'trace')) {
        $log = Join-Path $OutputDir ('render-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $extra = @()
        if ($mode -eq 'defer') { $extra += '--defer-cursor' }
        if ($mode -like 'menu*') { $extra += '--defer-menu' }
        if ($mode -like 'pause*') { $extra += '--redraw-pause' }
        if ($mode -like '*_fault') { $skipMenu.Set() | Out-Null } else { $skipMenu.Reset() | Out-Null }
        & (Join-Path $BinaryDir 'witness-probe-loader.exe') --render-trace --test-host @extra --pid $fixture.Id --log $log --duration-ms 300
        if ($LASTEXITCODE -ne 0) { throw 'Owned render capture failed' }
        $events = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        if ($events[-1].event -ne 'render.finished' -or !$events[-1].hooks_disabled -or !$events[-1].dll_pinned_until_exit) { throw 'Cleanup contract failed' }
        if ($events[-1].dropped -ne 0) { throw 'Unexpected trace loss' }
        foreach ($entry in @($events | Where-Object { $_.event -eq 'cursor.exit' })) {
            $snapshot = $entry.render_state
            if ($snapshot.current_target -ne $snapshot.eye_targets[$entry.eye] -or $snapshot.scene_source -ne '0x3330' -or
                $snapshot.menu_fade -ne 1 -or $snapshot.menu_matrix.Count -ne 16 -or $snapshot.menu_matrix[0] -ne 1 -or
                $null -ne $snapshot.menu_matrix[15]) { throw 'Draw-aligned render snapshot was lost, stale, or invalid JSON' }
        }
        $menus = @($events | Where-Object event -eq 'menu.exit')
        if (@($menus | Where-Object { $_.flags -eq 1 -and $_.eye -eq 0 }).Count -lt 5 -or
            @($menus | Where-Object { $_.flags -eq 1 -and $_.eye -eq 1 }).Count -lt 5 -or
            @($menus | Where-Object { $_.flags -eq 2 -and $_.eye -eq 0 }).Count -lt 5) { throw 'Menu hook lost stereo/mirror arguments or calls' }
        $submits = @($events | Where-Object event -eq 'eye.submit.immediate')
        $delayed = @($events | Where-Object event -eq 'eye.submit.after_cursor')
        $menuDelayed = @($events | Where-Object event -eq 'eye.submit.after_menu')
        $deferred = @($events | Where-Object event -eq 'eye.submit.deferred')
        $fallback = @($events | Where-Object event -eq 'eye.submit.fallback')
        if ($mode -notlike '*_fault' -and ($events[-1].route_fault -or $fallback.Count -ne 0)) { throw 'Unexpected render route fault' }
        $pauseChecks = @($events | Where-Object event -eq 'pause.render_check')
        if ($mode -eq 'pause') {
            if ($events[0].mode -ne 'redraw_paused_scene_and_defer_menu' -or
                @($pauseChecks | Where-Object flags -eq 3).Count -lt 5 -or
                @($pauseChecks | Where-Object flags -eq 0).Count -lt 5) { throw 'Scoped pause override not exercised' }
        } elseif ($mode -eq 'pause_fault') {
            if (@($pauseChecks | Where-Object flags -eq 1).Count -lt 5) { throw 'Render fault failed to turn off pause override' }
            $faultSeen = $false
            foreach ($entry in $events) {
                if ($entry.event -eq 'eye.submit.fallback') { $faultSeen = $true }
                if ($faultSeen -and $entry.event -eq 'pause.render_check' -and $entry.flags -eq 3) { throw 'Pause override continued after fault' }
            }
        } elseif ($pauseChecks.Count -ne 0) { throw 'Normal modes intercepted pause queries' }
        if ($mode -eq 'defer') {
            if ($delayed.Count -lt 5 -or $menuDelayed.Count -ne 0 -or $deferred.Count -ne $delayed.Count) { throw 'Cursor deferrals did not drain at the cursor' }
            for ($index = 0; $index -lt $events.Count; ++$index) {
                if ($events[$index].event -eq 'eye.submit.after_cursor' -and $events[$index-1].event -ne 'cursor.exit') { throw 'Deferred submit preceded cursor completion' }
            }
        } elseif ($mode -eq 'menu' -or $mode -eq 'pause') {
            if ($menuDelayed.Count -lt 5 -or $delayed.Count -ne 0 -or $deferred.Count -ne $menuDelayed.Count) { throw 'Menu deferrals did not drain at the menu' }
            for ($index = 1; $index -lt $events.Count; ++$index) {
                if ($events[$index].event -eq 'eye.submit.after_menu') {
                    $previous = $events[$index-1]
                    if ($previous.event -ne 'menu.exit' -or $previous.flags -ne 1 -or $previous.eye -ne $events[$index].eye) { throw 'Submission consumed by an incorrect menu pass' }
                }
            }
        } elseif ($mode -like '*_fault') {
            if (!$events[-1].route_fault -or $fallback.Count -ne 1 -or $deferred.Count -ne 1 -or $submits.Count -lt 5 -or $menuDelayed.Count -ne 0 -or $delayed.Count -ne 0) { throw 'Missing menu did not latch deferral off and recover immediate delivery' }
        } elseif ($submits.Count -lt 5 -or $delayed.Count -ne 0 -or $menuDelayed.Count -ne 0 -or $deferred.Count -ne 0) { throw 'Trace-only changed submission order' }
        $fixture.Refresh()
        if ($fixture.HasExited) { throw 'Owned render host did not survive' }
        if (@($fixture.Modules | Where-Object ModuleName -eq 'witness-render-probe.dll').Count -ne 1) { throw 'Expected retained render DLL missing' }
    }
} finally {
    $stop.Set() | Out-Null
    if (!$fixture.WaitForExit(5000)) { $fixture.Kill(); $fixture.WaitForExit() }
    $stop.Dispose()
    $skipMenu.Dispose()
}
if (!$fixture.HasExited -or $fixture.ExitCode -ne 0) { throw "Render host exit: HasExited=$($fixture.HasExited), ExitCode=$($fixture.ExitCode)" }
$actual = Get-Content -LiteralPath $metrics | ConvertFrom-Json
if ($actual.before_cursor -lt 5 -or $actual.after_cursor -lt 5 -or $actual.after_menu -lt 5 -or $actual.menu_calls -lt 15) { throw 'Independent host counters did not confirm actual reordering/menu passthrough' }
if ($actual.paused_redraws -lt 5 -or $actual.paused_skips -lt 5 -or $actual.pause_leaks -ne 0 -or $actual.unpaused_skips -ne 0) { throw 'Independent host counters failed pause call-site isolation or restoration' }
Write-Output 'Render hooks: cursor/menu deferral, scoped pause redraw, unrelated pause checks preserved, fault recovery, mode reset, restoration, retained DLL, and clean host exit passed.'
