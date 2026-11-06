param([string]$SignedPackageRoot = '')
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'check-autostart-integrity.ps1'
if (!(Test-Path -LiteralPath $scriptPath)) { throw 'Missing read-only autostart integrity diagnostic.' }
. $scriptPath
function Assert($condition, [string]$message) { if (!$condition) { throw $message } }
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ('lmu-integrity-' + [Guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($fixtureRoot) -ne $temporaryRoot -or [IO.Path]::GetFileName($fixtureRoot) -notmatch '^lmu-integrity-[a-f0-9]{32}$') { throw 'Unsafe test fixture location.' }
try {
    $package = Join-Path $fixtureRoot 'Package with spaces'
    $installed = Join-Path $fixtureRoot 'Protected fixture'
    New-Item -ItemType Directory -Path $package,$installed,(Join-Path $package 'build'),(Join-Path $package 'tools') -Force | Out-Null
    foreach ($relative in @('build/lmu_autostart.dll','build/lmu_quadview.dll','LMU-QuadView-Launcher.exe','tools/PortableStereoCheck.exe','LMU-QuadView-Launcher.exe.config')) {
        [IO.File]::WriteAllText((Join-Path $package $relative), 'fixture ' + $relative)
    }
    foreach ($name in @('lmu_autostart.dll','lmu_quadview.dll')) { Copy-Item -LiteralPath (Join-Path $package ('build/' + $name)) -Destination (Join-Path $installed $name) }
    $launcherHash = (Get-FileHash -LiteralPath (Join-Path $package 'LMU-QuadView-Launcher.exe') -Algorithm SHA256).Hash
    $configHash = (Get-FileHash -LiteralPath (Join-Path $package 'LMU-QuadView-Launcher.exe.config') -Algorithm SHA256).Hash
    [IO.File]::WriteAllText((Join-Path $installed 'launcher.sha256'), $launcherHash.ToLowerInvariant() + "`r`n")
    [IO.File]::WriteAllText((Join-Path $installed 'launcher-config.sha256'), $configHash + "`n")
    Assert (Test-LmuHashRecord (Join-Path $installed 'launcher.sha256') $launcherHash).Match 'Lowercase/trailing-newline approval record rejected.'
    $report = Get-LmuIntegrityReport $package $installed ''
    Assert ($report.ExitCode -eq 1) 'Unsigned fixture must fail integrity validation.'
    Assert ($report.PackageFiles.Count -eq 4 -and $report.InstalledFiles.Count -eq 2) 'Expected four package signatures and two installed DLL signatures.'
    Assert (@($report.InstalledFiles | Where-Object { !$_.MatchesPackage }).Count -eq 0) 'Identical installed DLLs did not match.'
    Assert (@($report.Approvals | Where-Object { !$_.Match }).Count -eq 0) 'Matching approval hashes rejected.'
    [IO.File]::AppendAllText((Join-Path $installed 'lmu_autostart.dll'), ' changed')
    [IO.File]::WriteAllText((Join-Path $installed 'launcher.sha256'), 'malformed')
    $report = Get-LmuIntegrityReport $package $installed ''
    Assert (!$report.InstalledFiles[0].MatchesPackage) 'Tampered installed DLL was accepted.'
    Assert (!$report.Approvals[0].Match -and $report.Approvals[0].Status -eq 'InvalidHashRecord') 'Malformed launcher approval was accepted.'
    Remove-Item -LiteralPath (Join-Path $installed 'launcher-config.sha256')
    Assert (!(Test-LmuHashRecord (Join-Path $installed 'launcher-config.sha256') $configHash).Match) 'Missing approval was accepted.'
    Assert ((Get-LmuIntegrityReport $package '' '').ExitCode -eq 2) 'Absent installation must return 2.'
    Assert ((Get-LmuIntegrityReport (Join-Path $fixtureRoot 'missing package') $installed '').ExitCode -eq 1) 'Missing package must return 1.'
    Assert ((Get-LmuIntegrityReport $package (Join-Path $fixtureRoot 'missing installed') '').ExitCode -eq 1) 'Explicit invalid installed path must return 1.'
    Assert ((Get-LmuIntegrityReport $package $installed (Join-Path $fixtureRoot 'Other package')).ActivationMatchesPackage -eq $false) 'Different activated package was accepted.'
    if ($SignedPackageRoot) {
        foreach ($name in @('lmu_autostart.dll','lmu_quadview.dll')) { Copy-Item -LiteralPath (Join-Path $SignedPackageRoot ('build/' + $name)) -Destination (Join-Path $installed $name) -Force }
        [IO.File]::WriteAllText((Join-Path $installed 'launcher.sha256'), (Get-FileHash -LiteralPath (Join-Path $SignedPackageRoot 'LMU-QuadView-Launcher.exe') -Algorithm SHA256).Hash)
        [IO.File]::WriteAllText((Join-Path $installed 'launcher-config.sha256'), (Get-FileHash -LiteralPath (Join-Path $SignedPackageRoot 'LMU-QuadView-Launcher.exe.config') -Algorithm SHA256).Hash)
        $signedReport = Get-LmuIntegrityReport $SignedPackageRoot $installed $SignedPackageRoot
        Assert ($signedReport.ExitCode -eq 0) ('Signed release fixture failed: ' + ($signedReport | ConvertTo-Json -Depth 6))
        Write-Output 'Signed release fixture PASS: six valid signatures, matching installed DLLs and launcher/config approvals.'
    }
    Write-Output 'Autostart integrity tests PASS: signature rejection, DLL mismatch, hash approvals, missing/invalid paths and activation mismatch. No live registry writes.'
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot -Recurse -Force }
}
