param([string]$BinaryDir,[string]$OutputDir)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$base='Local\WitnessStickTest-'+[guid]::NewGuid().ToString('N')
$events=@{}
foreach($n in @('','-Legacy','-Aim','-Stick','-Held','-Neutral','-Recenter','-Back')) {
    $events[$n]=New-Object System.Threading.EventWaitHandle($false,[System.Threading.EventResetMode]::ManualReset,($base+$n))
}
foreach($n in @('-Legacy','-Aim','-Stick')){$events[$n].Set()|Out-Null}
$metrics=Join-Path $OutputDir ('stick-host-'+[guid]::NewGuid().ToString('N')+'.json')
$fixture=Start-Process -FilePath (Join-Path $BinaryDir 'witness-input-test-host.exe') -ArgumentList $base -WindowStyle Hidden -PassThru -RedirectStandardOutput $metrics
$fixtureHandle=$fixture.Handle
$loader=Join-Path $BinaryDir 'witness-probe-loader.exe'
$configFile=Join-Path $BinaryDir ('input-settings-test-'+$fixture.Id+'.ini')
function Control([string]$Action) {
    $extra=@()
    if($Action -eq 'start'){$extra=@('--log',(Join-Path $OutputDir ('stick-session-'+[guid]::NewGuid().ToString('N')+'.jsonl')),'--controller-legacy-axis0','--controller-aim')}
    $output=& $loader --controller-session $Action --test-host --pid $fixture.Id @extra
    if($LASTEXITCODE -ne 0){throw "Stick control failed: $Action"}
    return ($output|ConvertFrom-Json)
}
try {
    $state=Control start
    Start-Sleep -Seconds 3
    $state=Control status
    if($state.stick_cursor_speed_percent -ne 40){throw 'Incorrect independent stick default'}
    if($state.stick_applied -lt 5 -or $state.cancel_suppressed -lt 5){throw 'Manual input/cancel filter did not run'}
    $beforePolls=$state.polls
    Set-Content -LiteralPath $configFile -Encoding Ascii -Value "[Input]`r`nStickCursorSpeedPercent=20"
    Start-Sleep -Milliseconds 1200
    $state=Control status
    if($state.stick_cursor_speed_percent -ne 20 -or $state.aim_speed_percent -ne 80 -or $state.aim_smoothing_ms -ne 80 -or $state.polls -le $beforePolls -or !$state.enabled){throw 'Live stick speed reload changed motion aiming or interrupted controls'}
    Set-Content -LiteralPath $configFile -Encoding Ascii -Value "[Input]`r`nStickCursorSpeedPercent=600`r`nMotionCursorSpeedPercent=25"
    Start-Sleep -Milliseconds 1200
    $state=Control status
    if($state.stick_cursor_speed_percent -ne 20 -or $state.aim_speed_percent -ne 25){throw 'Independent motion speed/invalid stick reload failed'}
    Set-Content -LiteralPath $configFile -Encoding Ascii -Value "[Input]`r`nStickCursorSpeedPercent=40`r`nMotionCursorSpeedPercent=80"
    Start-Sleep -Milliseconds 1200
    $state=Control status
    if($state.stick_cursor_speed_percent -ne 40 -or $state.aim_speed_percent -ne 80){throw 'Valid config did not recover after invalid edit'}
    $events['-Held'].Set()|Out-Null
    $events['-Neutral'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    $before=Control status
    Start-Sleep -Milliseconds 150
    $after=Control status
    if($after.stick_applied -ne $before.stick_applied -or $after.aim_applied -ne $before.aim_applied){throw 'Neutral manual cursor drifted or resumed pointing'}
    $events['-Recenter'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    $after=Control status
    if($after.recenter_count -ne ($before.recenter_count+1) -or $after.aim_applied -le $before.aim_applied){throw 'A failed to restore pointing'}
    $events['-Recenter'].Reset()|Out-Null
    $state=Control disable
    $events['-Neutral'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    $before=Control status
    Start-Sleep -Milliseconds 150
    $after=Control status
    if($after.stick_applied -ne $before.stick_applied -or $after.cancel_suppressed -ne $before.cancel_suppressed){throw 'Disabled input still affected stick or cancellation'}
    $state=Control enable
    Start-Sleep -Milliseconds 150
    if((Control status).stick_applied -ne $before.stick_applied){throw 'Held stick rearmed after toggle'}
    $events['-Neutral'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Neutral'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    if((Control status).stick_applied -le $before.stick_applied){throw 'Manual cursor failed to recover'}
    $backBefore=(Control status).puzzle_back_presses
    $events['-Back'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    if((Control status).puzzle_back_presses -ne ($backBefore+1)){throw 'Left B failed to request native puzzle back'}
    Start-Sleep -Milliseconds 200
    if((Control status).puzzle_back_presses -ne ($backBefore+1)){throw 'Held B repeated puzzle back'}
    $events['-Aim'].Reset()|Out-Null
    $events['-Back'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Back'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Aim'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    if((Control status).puzzle_back_presses -ne ($backBefore+1)){throw 'B fired in walking or survived entry into puzzle'}
    $events['-Back'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Back'].Set()|Out-Null
    Start-Sleep -Milliseconds 150
    if((Control status).puzzle_back_presses -ne ($backBefore+2)){throw 'Fresh B failed after puzzle entry'}
    $state=Control disable
    $events['-Back'].Reset()|Out-Null
    Start-Sleep -Milliseconds 150
    $events['-Back'].Set()|Out-Null
    $state=Control enable
    Start-Sleep -Milliseconds 150
    if((Control status).puzzle_back_presses -ne ($backBefore+2)){throw 'Held B fired after enabling input'}
    $before=Control stop
    Start-Sleep -Milliseconds 150
    $after=Control status
    if($after.stick_applied -ne $before.stick_applied -or $after.cancel_suppressed -ne $before.cancel_suppressed){throw 'Manual callbacks continued after stop'}
} finally {
    $events[''].Set()|Out-Null
    if(!$fixture.WaitForExit(5000)){$fixture.Kill();$fixture.WaitForExit()}
    foreach($e in $events.Values){$e.Dispose()}
    if(Test-Path -LiteralPath $configFile){Remove-Item -LiteralPath $configFile}
}
if($fixture.ExitCode -ne 0){throw "Stick host failed: $(Get-Content -LiteralPath $metrics)"}
$actual=Get-Content -LiteralPath $metrics|ConvertFrom-Json
if($actual.native_back_events -lt 2 -or $actual.native_back_leaks -ne 0 -or $actual.pad_filtered -lt 5 -or $actual.key_leaks -ne 0 -or $actual.aim_leaks -ne 0 -or $actual.aim_decoy_leaks -ne 0){throw 'Independent stick/filter checks failed'}
Write-Output 'Independent live speed settings, invalid-edit retention, left-B native puzzle back with held/context guards, manual cursor movement, neutral ownership, A return to pointing, trigger/menu and unrelated key-caller preservation, mode gates, toggles and teardown passed.'
