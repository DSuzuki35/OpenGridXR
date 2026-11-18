param([Parameter(Mandatory=$true)][string]$Helper)
$ErrorActionPreference='Stop'
$namespace='errors-'+[Guid]::NewGuid().ToString('N')
$name="Local\OpenGridXR-Test-$namespace-lmu"
function Check-Reason([string]$Expected) {
    # CI can give PowerShell a closed stdin pipe. The helper treats EOF as a
    # shutdown request, so own an open input pipe until its --once record arrives.
    $info=New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName=$Helper
    $info.Arguments="--game lmu --once --test-namespace $namespace"
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.WindowStyle=[System.Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardInput=$true
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    $process=New-Object System.Diagnostics.Process
    $process.StartInfo=$info
    try {
        if(-not $process.Start()){throw 'Telemetry helper failed to start'}
        $output=$process.StandardOutput.ReadToEndAsync()
        $errors=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(5000)){throw 'Telemetry diagnostic timed out'}
        if(-not $output.Wait(1000) -or -not $errors.Wait(1000)){throw 'Telemetry diagnostic output timed out'}
        if($process.ExitCode -ne 0){throw "Telemetry helper exited $($process.ExitCode): $($errors.Result)"}
        $records=@($output.Result -split "`n" | Where-Object { $_.Trim() } | ConvertFrom-Json)
        if($records.Count -ne 1){throw "Expected one telemetry diagnostic; got $($records.Count): $($errors.Result)"}
        $record=$records[0]
        if($record.reason -ne $Expected -or $record.status -ne 'disconnected' -or $record.connected -or $null -ne $record.data.speedMps){
            throw "Expected disconnected $Expected with no telemetry; got $($record.reason)"
        }
    } finally {
        if($process.Id -and -not $process.HasExited){$process.Kill()}
        $process.Dispose()
    }
}
Check-Reason 'mapping-not-found'
$security=New-Object System.IO.MemoryMappedFiles.MemoryMappedFileSecurity
$security.SetSecurityDescriptorSddlForm('D:(D;;0x4;;;WD)')
$mapping=[System.IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($name,4096,[System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite,[System.IO.MemoryMappedFiles.MemoryMappedFileOptions]::None,$security,[System.IO.HandleInheritability]::None)
try { Check-Reason 'mapping-access-denied' } finally { $mapping.Dispose() }
$event=[System.Threading.EventWaitHandle]::new($false,[System.Threading.EventResetMode]::ManualReset,$name)
try { Check-Reason 'mapping-open-failed' } finally { $event.Dispose() }
Write-Output 'Telemetry distinguishes absent, inaccessible, and incompatible mapping objects'
