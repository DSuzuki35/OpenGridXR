param(
    [string]$PackageRoot = (Join-Path $PSScriptRoot '..'),
    [string]$InstalledDirectory = '',
    [string]$OutputPath = ''
)
# Read-only diagnostic. Dot-sourcing loads its functions without inspecting the machine.
$ErrorActionPreference = 'Stop'
$script:LmuExpectedSigner = '89F7F00A4FD4BD66F5617F1202B8E7B9DD662247'

function Get-LmuFileRecord([string]$Path) {
    $record = [ordered]@{ Path=$Path; SHA256=''; SignatureStatus='Missing'; SignerThumbprint=''; TimestampThumbprint=''; Valid=$false; Error='' }
    try {
        if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return [pscustomobject]$record }
        $record.SHA256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        $signature = Get-AuthenticodeSignature -LiteralPath $Path
        $record.SignatureStatus = $signature.Status.ToString()
        if ($signature.SignerCertificate) { $record.SignerThumbprint = $signature.SignerCertificate.Thumbprint }
        if ($signature.TimeStamperCertificate) { $record.TimestampThumbprint = $signature.TimeStamperCertificate.Thumbprint }
        $record.Valid = $record.SignatureStatus -eq 'Valid' -and $record.SignerThumbprint -eq $script:LmuExpectedSigner -and !!$record.TimestampThumbprint
    } catch { $record.Error = $_.Exception.Message }
    return [pscustomobject]$record
}

function Test-LmuHashRecord([string]$Path, [string]$ExpectedHash) {
    $result = [ordered]@{ Path=$Path; ExpectedSHA256=$ExpectedHash; RecordedSHA256=''; Match=$false; Status='Missing'; Error='' }
    try {
        if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return [pscustomobject]$result }
        $result.RecordedSHA256 = [IO.File]::ReadAllText($Path).Trim()
        if ($result.RecordedSHA256 -notmatch '^[a-fA-F0-9]{64}$') { $result.Status='InvalidHashRecord' }
        elseif ($ExpectedHash -match '^[a-fA-F0-9]{64}$' -and $result.RecordedSHA256 -eq $ExpectedHash) { $result.Match=$true; $result.Status='Match' }
        else { $result.Status='Mismatch' }
    } catch { $result.Status='ReadError'; $result.Error=$_.Exception.Message }
    return [pscustomobject]$result
}

function Get-LmuIntegrityReport([string]$Root, [string]$Installed, [string]$ActivationRoot) {
    $report = [ordered]@{
        PackageRoot=$Root; InstalledDirectory=$Installed; ActivatedPackageRoot=$ActivationRoot
        ActivationMatchesPackage=$true; ExpectedSigner=$script:LmuExpectedSigner
        PackageFiles=@(); InstalledFiles=@(); Approvals=@(); ConfigurationSHA256=''
        ExitCode=1; Summary='Integrity validation failed.'; Errors=@()
        Note='A valid publisher signature identifies the publisher and file integrity; it does not establish Easy Anti-Cheat approval.'
    }
    try {
        $Root = [IO.Path]::GetFullPath($Root); $report.PackageRoot=$Root
        if (!(Test-Path -LiteralPath $Root -PathType Container)) { throw 'Package directory does not exist.' }
        if ($ActivationRoot) { $report.ActivationMatchesPackage = [IO.Path]::GetFullPath($ActivationRoot).TrimEnd('\') -eq $Root.TrimEnd('\') }
        $report.PackageFiles = @(foreach ($relative in @('build/lmu_autostart.dll','build/lmu_quadview.dll','LMU-QuadView-Launcher.exe','tools/PortableStereoCheck.exe')) { Get-LmuFileRecord (Join-Path $Root $relative) })
        $config = Join-Path $Root 'LMU-QuadView-Launcher.exe.config'
        if (Test-Path -LiteralPath $config -PathType Leaf) { $report.ConfigurationSHA256=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash }
        if (!$Installed) {
            $report.ExitCode=2; $report.Summary='No enabled LMU automatic-start installation was found.'
            return [pscustomobject]$report
        }
        $Installed=[IO.Path]::GetFullPath($Installed); $report.InstalledDirectory=$Installed
        if (!(Test-Path -LiteralPath $Installed -PathType Container)) { throw 'Installed directory does not exist.' }
        $report.InstalledFiles = @(for ($i=0; $i -lt 2; $i++) {
            $file=Get-LmuFileRecord (Join-Path $Installed ([IO.Path]::GetFileName($report.PackageFiles[$i].Path)))
            $file | Add-Member -NotePropertyName MatchesPackage -NotePropertyValue (!!$file.SHA256 -and $file.SHA256 -eq $report.PackageFiles[$i].SHA256)
            $file
        })
        $report.Approvals = @(
            Test-LmuHashRecord (Join-Path $Installed 'launcher.sha256') $report.PackageFiles[2].SHA256
            Test-LmuHashRecord (Join-Path $Installed 'launcher-config.sha256') $report.ConfigurationSHA256
        )
        $invalid = @($report.PackageFiles | Where-Object { !$_.Valid }).Count + @($report.InstalledFiles | Where-Object { !$_.Valid -or !$_.MatchesPackage }).Count + @($report.Approvals | Where-Object { !$_.Match }).Count
        if ($invalid -eq 0 -and $report.ActivationMatchesPackage) {
            $report.ExitCode=0; $report.Summary='Publisher signatures, installed DLLs and launcher approvals match this package.'
        }
    } catch { $report.Errors += $_.Exception.Message }
    return [pscustomobject]$report
}

function Get-LmuAutostartLocation {
    $activation=''; $installed=''; $manifest=''; $order=$false
    $ini=Join-Path $env:LOCALAPPDATA 'LMUQuadView/autoload.ini'
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        $section=''
        foreach ($line in [IO.File]::ReadAllLines($ini)) {
            if ($line -match '^\s*\[([^\]]+)\]\s*$') { $section=$Matches[1] }
            elseif ($section -eq 'AutoStart' -and $line -match '^PackageRoot=(.*)$') { $activation=$Matches[1]; break }
        }
    }
    if (!$activation) {
        $user=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\LMUQuadView\AutoStart')
        if ($user) { try { $activation=[string]$user.GetValue('PackageRoot','') } finally { $user.Dispose() } }
    }
    $hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key=$hive.OpenSubKey('SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit')
        if ($key) { try {
            $programFiles=$env:ProgramW6432
            if (!$programFiles) { $programFiles=$env:ProgramFiles }
            $first=''; $protectedRoot=[IO.Path]::GetFullPath((Join-Path $programFiles 'LMU QuadView Layer')) + '\'
            foreach ($name in $key.GetValueNames()) {
                $value=$key.GetValue($name)
                if ($value -isnot [int] -or $value -ne 0) { continue }
                if (!$first) { $first=$name }
                try { $full=[IO.Path]::GetFullPath($name) } catch { continue }
                if ($full.StartsWith($protectedRoot,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($full) -eq 'XR_APILAYER_LMU_autostart.json') {
                    $manifest=$full; $installed=[IO.Path]::GetDirectoryName($full); $order=$first -eq $name; break
                }
            }
        } finally { $key.Dispose() } }
    } finally { $hive.Dispose() }
    return [pscustomobject]@{ ActivationRoot=$activation; InstalledDirectory=$installed; Manifest=$manifest; FirstEnabledLayer=$order }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        $activation=''; $location=$null
        if (!$InstalledDirectory) { $location=Get-LmuAutostartLocation; $InstalledDirectory=$location.InstalledDirectory; $activation=$location.ActivationRoot }
        $report=Get-LmuIntegrityReport $PackageRoot $InstalledDirectory $activation
        if ($location) {
            $report | Add-Member -NotePropertyName RegisteredManifest -NotePropertyValue $location.Manifest
            $report | Add-Member -NotePropertyName FirstEnabledLayer -NotePropertyValue $location.FirstEnabledLayer
            if ($InstalledDirectory -and !$location.FirstEnabledLayer) { $report.ExitCode=1; $report.Summary='LMU automatic startup is not the first enabled OpenXR layer.' }
            elseif ($InstalledDirectory -and !$activation) { $report.ExitCode=1; $report.Summary='No automatic-start activation exists for the current user.' }
        }
        $json=$report | ConvertTo-Json -Depth 6
        if ($OutputPath) { [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath),$json,[Text.UTF8Encoding]::new($false)) }
        Write-Output $json
        exit $report.ExitCode
    } catch { Write-Error $_ -ErrorAction Continue; exit 1 }
}
