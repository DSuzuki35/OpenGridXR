param([string]$Helper,[string]$Fixture,[ValidateRange(0,5000)][int]$ReaderDelayMs=0)
$ErrorActionPreference='Stop'
function Launch([string]$Path,[string]$Arguments) {
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$Path
    $info.Arguments=$Arguments
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.WindowStyle=[System.Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    $info.RedirectStandardInput=$true
    $process=New-Object System.Diagnostics.Process
    $process.StartInfo=$info
    if(-not $process.Start()){throw 'process failed to start'}
    return $process
}
function Require($Value,[string]$Message){if(-not $Value){throw $Message}}
function Read-Record($Process,[int]$TimeoutMs,[string]$Message) {
    Require ($TimeoutMs -gt 0) $Message
    $task=$Process.StandardOutput.ReadLineAsync()
    Require ($task.Wait($TimeoutMs)) $Message
    Require ($null -ne $task.Result) "$Message (process output closed)"
    return ($task.Result | ConvertFrom-Json)
}
$namespace='io-'+[Guid]::NewGuid().ToString('N')
$writer=$null;$reader=$null;$freezeEvent=$null
try {
    $freezeEvent=[System.Threading.EventWaitHandle]::new($false,[System.Threading.EventResetMode]::ManualReset,"Local\OpenGridXR-Test-$namespace-freeze")
    $writer=Launch $Fixture "--namespace $namespace --duration-ms 30000 --freeze-event $namespace"
    $ready=Read-Record $writer 5000 'fixture readiness is bounded'
    Require $ready.ready 'fixture producer ready'
    # Deliberate startup delay is a regression injection, not synchronization.
    # The producer keeps advancing until the reader has actually observed live data.
    if($ReaderDelayMs -gt 0){Start-Sleep -Milliseconds $ReaderDelayMs}
    $reader=Launch $Helper "--game auto --rate 20 --test-namespace $namespace --duration-ms 15000"
    $records=New-Object 'System.Collections.Generic.List[object]'
    $liveCounts=@{iracing=0;lmu=0};$staleSeen=@{iracing=$false;lmu=$false}
    $phase=[System.Diagnostics.Stopwatch]::StartNew()
    while($liveCounts.iracing -le 1 -or $liveCounts.lmu -le 1){
        $row=Read-Record $reader ([int](5000-$phase.ElapsedMilliseconds)) 'both games must produce live updates before freeze'
        $records.Add($row)
        if($row.status -eq 'live' -and $liveCounts.ContainsKey($row.game)){$liveCounts[$row.game]++}
    }
    [void]$freezeEvent.Set()
    $phase.Restart()
    while(-not $staleSeen.iracing -or -not $staleSeen.lmu){
        $row=Read-Record $reader ([int](5000-$phase.ElapsedMilliseconds)) 'both frozen games must become stale'
        $records.Add($row)
        if($row.status -eq 'stale' -and $row.ageMs -ge 1000 -and $staleSeen.ContainsKey($row.game)){$staleSeen[$row.game]=$true}
    }
    $readTask=$reader.StandardOutput.ReadToEndAsync()
    $reader.StandardInput.WriteLine('quit');$reader.StandardInput.Flush()
    Require ($reader.WaitForExit(2000)) 'helper bounded shutdown after stale samples'
    Require ($reader.ExitCode -eq 0) 'helper exit success'
    Require ($readTask.Wait(1000)) 'helper output drain is bounded'
    foreach($line in ($readTask.Result -split "`n" | Where-Object { $_.Trim() })){$records.Add(($line | ConvertFrom-Json))}
    foreach($game in @('iracing','lmu')) {
        $rows=@($records | Where-Object {$_.game -eq $game})
        $live=@($rows | Where-Object {$_.status -eq 'live'})
        Require ($live.Count -gt 1) "$game has actual fixture updates through shared memory"
        # Use the actual emitted interval because the live-to-stale handshake is
        # intentionally independent of process startup speed. The three-record
        # allowance covers the initial record and immediate status transitions.
        $intervalMs=[double]($rows[-1].timestampMs-$rows[0].timestampMs)
        $maxRecords=[Math]::Ceiling($intervalMs*20/1000)+3
        Require ($rows.Count -le $maxRecords) "$game obeys update-rate cap"
        Require (@($rows | Where-Object {$_.status -eq 'stale' -and $_.ageMs -ge 1000}).Count -gt 0) "$game freezes become stale"
        foreach($row in $live){
            Require ($row.testData -eq $true -and $row.schemaVersion -eq 1) 'test records unmistakably marked'
            Require ($row.data.speedMps -eq 50 -and $row.data.gear -eq 4 -and $row.data.rpm -eq 6500) 'normalized live fixture values'
            Require ($row.data.fuelLiters -eq 42) 'fuel unit normalization'
        }
    }
    $reader.Dispose();$reader=Launch $Helper "--game auto --once --test-namespace missing-$namespace"
    $emptyTask=$reader.StandardOutput.ReadToEndAsync()
    Require ($reader.WaitForExit(2000)) 'missing mappings do not block startup'
    Require ($emptyTask.Wait(1000)) 'missing-mapping output drain is bounded'
    $empty=$emptyTask.Result
    foreach($line in ($empty -split "`n" | Where-Object { $_.Trim() })){
        $row=$line|ConvertFrom-Json
        Require ($row.status -eq 'disconnected' -and $null -eq $row.data.speedMps) 'no fabricated disconnected values'
    }
    $reader.Dispose();$reader=Launch $Helper '--game auto --rate 31'
    Require ($reader.WaitForExit(2000)) 'bad rate rejected promptly'
    Require ($reader.ExitCode -ne 0) 'rate cap cannot be bypassed'
    $reader.Dispose();$reader=Launch $Helper "--test-namespace missing-$namespace"
    $reader.StandardInput.WriteLine('quit');$reader.StandardInput.Flush()
    Require ($reader.WaitForExit(1500)) 'stdin shutdown stops idle helper'
    Write-Output 'Telemetry helper shared-memory, rate, stale, no-source and shutdown tests passed'
} finally {
    foreach($process in @($reader,$writer)){if($null -ne $process){if(-not $process.HasExited){$process.Kill()};$process.Dispose()}}
    if($null -ne $freezeEvent){$freezeEvent.Dispose()}
}
