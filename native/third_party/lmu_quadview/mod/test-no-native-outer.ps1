param([string]$GameDirectory)
$ErrorActionPreference = 'Stop'
if (!$GameDirectory) {
    $settings = Join-Path $PSScriptRoot 'launcher-settings.xml'
    if (Test-Path -LiteralPath $settings) {
        [xml]$saved = Get-Content -LiteralPath $settings -Raw
        $GameDirectory = [string]$saved.QuadView.GameDirectory
    }
}
if (!$GameDirectory) { throw 'First select the game folder and Save settings in the launcher, or pass -GameDirectory.' }
& (Join-Path $PSScriptRoot 'launch.ps1') -Mode quad -UseRuntimeSettings -DebugFocus -GameDirectory $GameDirectory
