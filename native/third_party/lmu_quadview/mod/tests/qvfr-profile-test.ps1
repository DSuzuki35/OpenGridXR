$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '../setup-qvfr-profile.ps1'
. $script -DefineOnly
$original = "# existing settings`n[exe:DCS.exe]`nfocus_multiplier=1.7`n[exe:Le Mans Ultimate.exe]`nprefer_foveated_rendering=1`n"
$result = New-LmuQvfrSettingsText $original
if (!$result.StartsWith($original.TrimEnd("`n"))) { throw 'Existing settings changed' }
if ($result -cne (New-LmuQvfrSettingsText $result)) { throw 'Profile not idempotent' }
foreach ($bad in @('# BEGIN LMU QUAD VIEW MOD PROFILE', '# END LMU QUAD VIEW MOD PROFILE', "# END LMU QUAD VIEW MOD PROFILE`n# BEGIN LMU QUAD VIEW MOD PROFILE")) {
    $rejected=$false
    try { New-LmuQvfrSettingsText $bad | Out-Null } catch { $rejected=$true }
    if (!$rejected) { throw 'Malformed block accepted' }
}
$directory = Join-Path $PSScriptRoot ('../build/profile-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory -Force | Out-Null
$path = Join-Path $directory 'settings.cfg'
[IO.File]::WriteAllText($path,$original)
& $script -SettingsPath $path -WhatIf
if ([IO.File]::ReadAllText($path) -cne $original) { throw 'WhatIf modified settings' }
& $script -SettingsPath $path
$backup = @(Get-ChildItem -LiteralPath $directory -Filter '*.lmu-backup-*')
if ($backup.Count -ne 1 -or [IO.File]::ReadAllText($backup[0].FullName) -cne $original) { throw 'Backup incorrect' }
if ([IO.File]::ReadAllText($path) -cne $result) { throw 'Written settings incorrect' }
& $script -SettingsPath $path
if (@(Get-ChildItem -LiteralPath $directory -Filter '*.lmu-backup-*').Count -ne 1) { throw 'Idempotent run rewrote settings' }
Write-Output 'QVFR profile tests passed: preservation, malformed blocks, WhatIf, backup, idempotency.'
