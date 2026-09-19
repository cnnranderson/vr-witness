param([string]$BinaryDir, [string]$OutputDir)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$eventName = 'Local\WitnessSnapTest-' + [guid]::NewGuid().ToString('N')
$events = @{}
foreach ($name in @('','-Snap','-Legacy','-Held','-Neutral','-Aim')) {
    $events[$name] = New-Object System.Threading.EventWaitHandle($false, [System.Threading.EventResetMode]::ManualReset, ($eventName + $name))
}
$events['-Snap'].Set() | Out-Null
$metrics = Join-Path $OutputDir ('snap-host-' + [guid]::NewGuid().ToString('N') + '.json')
$fixture = Start-Process -FilePath (Join-Path $BinaryDir 'witness-input-test-host.exe') -ArgumentList $eventName -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
$fixtureHandle = $fixture.Handle
$loader = Join-Path $BinaryDir 'witness-probe-loader.exe'
function Control([string]$Action, [bool]$Snap = $true) {
    $extra = @()
    if ($Action -eq 'start') {
        $log = Join-Path $OutputDir ('snap-session-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $extra = @('--log',$log,'--controller-legacy-axis0','--controller-aim')
        if ($Snap) { $extra += @('--controller-snap','--snap-angle','90') }
    }
    $output = & $loader --controller-session $Action --test-host --pid $fixture.Id @extra
    if ($LASTEXITCODE -ne 0) { throw "Snap session command failed: $Action" }
    return ($output | ConvertFrom-Json)
}
try {
    foreach ($mode in @('trace','45','trace','legacy_rejected','22.5','90')) {
        $log = Join-Path $OutputDir ('snap-' + $mode + '-' + [guid]::NewGuid().ToString('N') + '.jsonl')
        $extra = @('--controller-snap','--snap-angle',$mode)
        if ($mode -eq 'trace') { $extra = @('--controller-snap-trace') }
        if ($mode -eq 'legacy_rejected') { $events['-Legacy'].Set() | Out-Null; $extra = @('--controller-snap') }
        if ($mode -in @('22.5','90')) { $extra += '--controller-legacy-axis0' }
        & $loader --controller-trace --test-host --pid $fixture.Id --log $log --duration-ms 2500 @extra
        if ($LASTEXITCODE -ne 0) { throw "Snap capture failed: $mode" }
        $records = @(Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json })
        $last = $records[-1]
        if ($last.event -ne 'input.finished' -or !$last.hooks_disabled -or $last.turn_calls -lt 100 -or $last.applied -ne 0 -or $last.aim_applied -ne 0) { throw 'Snap capture/lifetime isolation failed' }
        if ($mode -in @('trace','legacy_rejected')) {
            if ($last.turn_applied -ne 0) { throw 'Passive/unrecognized axis changed heading' }
        } elseif ($last.turn_applied -lt 1) { throw "No turns for $mode" }
        $route = @($records | Where-Object { $_.event -eq 'input.sample' -and $_.turn_ready -and $_.turn_route_calls -gt 0 })
        if ($route.Count -lt 5) { throw 'No fresh caller-scoped native VR update route' }
    }
    $events['-Held'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $state = Control start
    if (!$state.snap_enabled -or $state.snap_angle -ne 90 -or !$state.aim_enabled) { throw 'Persistent snap configuration lost' }
    Start-Sleep -Milliseconds 200
    if ((Control status).turn_applied -ne 0) { throw 'Held stick turned on session entry' }
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).turn_applied -ne 1) { throw 'Neutral/deflection did not turn once' }
    Start-Sleep -Milliseconds 350
    if ((Control status).turn_applied -ne 1) { throw 'Held stick repeated after cooldown' }
    $state = Control disable
    $before = $state.turn_applied
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    $state = Control enable
    Start-Sleep -Milliseconds 150
    if ((Control status).turn_applied -ne $before) { throw 'Disabled neutral rearmed snap' }
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).turn_applied -ne ($before+1)) { throw 'Reenabled snap did not recover after neutral' }
    $events['-Aim'].Set() | Out-Null
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    $before = (Control status).turn_applied
    $events['-Aim'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).turn_applied -ne $before) { throw 'Puzzle-exit held stick caused a turn' }
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    if ((Control status).turn_applied -ne ($before+1)) { throw 'Snap did not recover after puzzle/neutral' }
    $state = Control stop
    $before = $state.turn_applied; $beforeCalls = $state.turn_calls
    Start-Sleep -Milliseconds 150
    $state = Control status
    if ($state.turn_applied -ne $before -or $state.turn_calls -ne $beforeCalls -or $state.snap_enabled) { throw 'Turn callbacks continued after stop' }
    $state = Control start $false
    $events['-Neutral'].Set() | Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset() | Out-Null
    Start-Sleep -Milliseconds 150
    $state = Control stop
    if ($state.turn_calls -ne 0 -or $state.turn_applied -ne 0 -or $state.snap_angle -ne 0) { throw 'Restart without snap retained the VR hook' }
} finally {
    $events[''].Set() | Out-Null
    if (!$fixture.WaitForExit(5000)) { $fixture.Kill(); $fixture.WaitForExit() }
    foreach ($event in $events.Values) { $event.Dispose() }
}
if ($fixture.ExitCode -ne 0) { throw "Snap fixture exit $($fixture.ExitCode): $(Get-Content -LiteralPath $metrics)" }
$actual = Get-Content -LiteralPath $metrics | ConvertFrom-Json
if ($actual.turns -lt 5 -or $actual.angle22 -lt 1 -or $actual.angle45 -lt 1 -or $actual.angle90 -lt 1 -or $actual.turn_leaks -ne 0 -or $actual.turn_decoy_leaks -ne 0 -or $actual.turn_repeat_leaks -ne 0 -or $actual.turn_order_leaks -ne 0) { throw 'Independent snap fixture counters failed' }
Write-Output 'Snap: paired heading values and sign, all angles, walking/puzzle/pause/focus/device gates, neutral rearm, one turn per deflection, original VR update ordering, caller isolation, passive capture, session toggles/restart and hook teardown passed.'
