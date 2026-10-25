param([string]$Helper,[string]$Fixture)
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
$namespace='io-'+[Guid]::NewGuid().ToString('N')
$writer=$null;$reader=$null
try {
    $writer=Launch $Fixture "--namespace $namespace --duration-ms 6000 --freeze-after-ms 500"
    $ready=$writer.StandardOutput.ReadLine() | ConvertFrom-Json
    Require $ready.ready 'fixture producer ready'
    $reader=Launch $Helper "--game auto --rate 20 --test-namespace $namespace --duration-ms 2200"
    $readTask=$reader.StandardOutput.ReadToEndAsync()
    Require ($reader.WaitForExit(5000)) 'helper bounded duration'
    Require ($reader.ExitCode -eq 0) 'helper exit success'
    $records=@($readTask.Result -split "`n" | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    foreach($game in @('iracing','lmu')) {
        $rows=@($records | Where-Object {$_.game -eq $game})
        $live=@($rows | Where-Object {$_.status -eq 'live'})
        Require ($live.Count -gt 1) "$game has actual fixture updates through shared memory"
        Require ($rows.Count -le 47) "$game obeys update-rate cap"
        Require (@($rows | Where-Object {$_.status -eq 'stale' -and $_.ageMs -ge 1000}).Count -gt 0) "$game freezes become stale"
        foreach($row in $live){
            Require ($row.testData -eq $true -and $row.schemaVersion -eq 1) 'test records unmistakably marked'
            Require ($row.data.speedMps -eq 50 -and $row.data.gear -eq 4 -and $row.data.rpm -eq 6500) 'normalized live fixture values'
            Require ($row.data.fuelLiters -eq 42) 'fuel unit normalization'
        }
    }
    $reader.Dispose();$reader=Launch $Helper "--game auto --once --test-namespace missing-$namespace"
    $empty=$reader.StandardOutput.ReadToEnd()
    Require ($reader.WaitForExit(2000)) 'missing mappings do not block startup'
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
}
