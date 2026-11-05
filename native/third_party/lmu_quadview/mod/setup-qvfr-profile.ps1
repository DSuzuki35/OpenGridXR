[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$SettingsPath = (Join-Path $env:LOCALAPPDATA 'Quad-Views-Foveated/settings.cfg'),
    [switch]$DefineOnly
)
function New-LmuQvfrSettingsText([string]$Existing) {
    $begin = '# BEGIN LMU QUAD VIEW MOD PROFILE'
    $end = '# END LMU QUAD VIEW MOD PROFILE'
    $starts = [regex]::Matches($Existing, [regex]::Escape($begin)).Count
    $ends = [regex]::Matches($Existing, [regex]::Escape($end)).Count
    if ($starts -ne $ends -or $starts -gt 1) { throw 'Malformed LMU profile markers; settings left unchanged.' }
    if ($starts) {
        $pattern = '(?ms)^' + [regex]::Escape($begin) + '\r?$.*?^' + [regex]::Escape($end) + '\r?$(?:\n)?'
        $clean = [regex]::Replace($Existing, $pattern, '')
        if ($clean -eq $Existing) { throw 'Malformed LMU profile block; settings left unchanged.' }
    } else { $clean = $Existing }
    $profile = @'
# BEGIN LMU QUAD VIEW MOD PROFILE
[exe:Le Mans Ultimate.exe]
prefer_foveated_rendering=0
force_no_eye_tracking=0
peripheral_multiplier=0.25
focus_multiplier=1.0
horizontal_focus_section=0.5
vertical_focus_section=0.5
horizontal_fixed_section=0.5
vertical_fixed_section=0.5
turbo_mode=0
# END LMU QUAD VIEW MOD PROFILE
'@
    return $clean.TrimEnd("`r", "`n") + "`n`n" + $profile + "`n"
}
if ($DefineOnly) { return }
$ErrorActionPreference = 'Stop'
$SettingsPath = [IO.Path]::GetFullPath($SettingsPath)
$existing = if (Test-Path -LiteralPath $SettingsPath) { [IO.File]::ReadAllText($SettingsPath) } else { '' }
$updated = New-LmuQvfrSettingsText $existing
if ($updated -ceq $existing) { Write-Output 'LMU QVFR profile already applied.'; return }
if ($PSCmdlet.ShouldProcess($SettingsPath, 'Append/update LMU-only QVFR profile with backup')) {
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($SettingsPath)) -Force | Out-Null
    if (Test-Path -LiteralPath $SettingsPath) {
        $backup = $SettingsPath + '.lmu-backup-' + [Guid]::NewGuid().ToString('N')
        Copy-Item -LiteralPath $SettingsPath -Destination $backup
        Write-Output "Previous settings backed up to: $backup"
    }
    [IO.File]::WriteAllText($SettingsPath, $updated, [Text.UTF8Encoding]::new($false))
    Write-Output 'LMU-only profile applied. Restart LMU. Other application sections are preserved.'
}
