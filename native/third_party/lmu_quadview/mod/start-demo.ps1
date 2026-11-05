# Pimax / OpenXR supplies focus FOV and inner/outer resolutions.
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'launch.ps1') -Mode quad -NativeOuterResolution -UseRuntimeSettings
