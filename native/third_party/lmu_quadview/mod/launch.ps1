param(
    [ValidateSet('trace','quad','stereo-timing')][string]$Mode = 'trace',
    [switch]$PixelStats,
    [switch]$ScenePixelStats,
    [switch]$CaptureViews,
    [switch]$GpuTiming,
    [switch]$RenderProfiling,
    [switch]$DisableDiagnostics,
    [switch]$StereoDrawAudit,
    [switch]$StereoShaderCapture,
    [switch]$StereoPairProof,
    [switch]$CpuUploadReuse,
    [switch]$CpuOuterReuse,
    [ValidateRange(30,300)][int]$RenderProfileSeconds=120,
    [switch]$DebugFocus,
    [switch]$Benchmark,
    [switch]$EnableOuterVrs,
    [switch]$NativeOuterResolution,
    [switch]$OuterSsr,
    [switch]$UseRuntimeSettings,
    [ValidateSet('Native','QVFR','VectorXR')][string]$Backend='Native',
    [ValidateSet('auto','fixed')][string]$FoveationMode='auto',
    [Alias('VectorXrManifest')][string]$QuadViewsManifest,
    [ValidateSet(1,2,4)][int]$OuterShadingRate=2,
    [ValidateRange(1,400)][double]$OuterPixelsPercent=100,
    [ValidateRange(25,400)][double]$FocusPixelsPercent=100,
    [ValidateRange(25,200)][double]$FocusBoxPercent=100,
    [ValidateRange(25,200)][double]$FocusWidthPercent=100,
    [ValidateRange(25,200)][double]$FocusHeightPercent=100,
    [ValidateRange(-50,50)][double]$FocusOffsetXPercent=0,
    [ValidateRange(-50,50)][double]$FocusOffsetYPercent=0,
    [string]$GameDirectory = 'D:\SteamLibrary\steamapps\common\Le Mans Ultimate'
)
$ErrorActionPreference = 'Stop'
if($DisableDiagnostics){$RenderProfiling=$false;$GpuTiming=$false;$Benchmark=$false;$CaptureViews=$false;$PixelStats=$false;$ScenePixelStats=$false;$StereoDrawAudit=$false;$StereoShaderCapture=$false;$StereoPairProof=$false}
. (Join-Path $PSScriptRoot 'portable-launch.ps1')
if ($Backend -ne 'Native' -and ($Mode -ne 'quad' -or !$UseRuntimeSettings)) { throw "$Backend requires quad mode with runtime-controlled settings." }
if ($UseRuntimeSettings) {
    if ($Mode -ne 'quad') { throw 'Runtime-controlled settings require quad mode' }
    foreach ($name in @('OuterPixelsPercent','FocusPixelsPercent','FocusBoxPercent','FocusWidthPercent','FocusHeightPercent')) {
        if ($PSBoundParameters.ContainsKey($name)) { throw "UseRuntimeSettings takes resolution and FOV from OpenXR; remove -$name" }
    }
}
if ($Mode -eq 'stereo-timing' -and !$DisableDiagnostics) { $GpuTiming = $true }
if ($GpuTiming -and $Mode -eq 'trace') { throw 'Use stereo-timing for native stereo GPU measurements; trace has no engine hooks' }
if($RenderProfiling -and ($Mode -ne 'quad' -or $GpuTiming -or $Benchmark)){throw 'RenderProfiling requires quad mode without legacy GPU timing or Benchmark.'}
if ($DebugFocus -and $Mode -ne 'quad') { throw 'Focus debug border requires quad mode' }
if (($Benchmark -or $EnableOuterVrs) -and $Mode -ne 'quad') { throw 'Outer VRS experiments and A/B benchmarking require quad mode' }
if ($NativeOuterResolution -and ($Mode -ne 'quad' -or $EnableOuterVrs)) { throw 'Initial native outer resolution experiment requires quad mode with VRS disabled' }
if ($OuterSsr -and ($Mode -ne 'quad' -or !$NativeOuterResolution)) { throw 'OuterSsr requires quad mode with NativeOuterResolution.' }
if ($CpuOuterReuse -and ($Mode -ne 'quad' -or $StereoDrawAudit -or $StereoShaderCapture -or $StereoPairProof)) { throw 'CpuOuterReuse requires quad mode without stereo audit, shader capture or pair proof.' }
if ($CpuOuterReuse) { $CpuUploadReuse = $true }
if ($Mode -ne 'quad' -and ($OuterPixelsPercent -ne 100 -or $FocusPixelsPercent -ne 100)) { throw 'Separate view sizes require quad mode' }
if ($OuterPixelsPercent -gt $FocusPixelsPercent) { throw 'Outer output must not exceed the common focus render size' }
if (!$UseRuntimeSettings -and $NativeOuterResolution -and $OuterPixelsPercent -ge $FocusPixelsPercent) { throw 'Native outer resolution requires outer pixels below focus pixels' }
if (!$PSBoundParameters.ContainsKey('FocusWidthPercent')) { $FocusWidthPercent=$FocusBoxPercent }
if (!$PSBoundParameters.ContainsKey('FocusHeightPercent')) { $FocusHeightPercent=$FocusBoxPercent }
if ($Mode -ne 'quad' -and ($FocusWidthPercent -ne 100 -or $FocusHeightPercent -ne 100)) { throw 'Focus box size requires quad mode' }
foreach($offset in @($FocusOffsetXPercent,$FocusOffsetYPercent)) {
    if([double]::IsNaN($offset) -or [double]::IsInfinity($offset) -or $offset -lt -50 -or $offset -gt 50) {throw 'Focus offsets must be finite numbers between -50 and +50 percent'}
}
if($Mode -ne 'quad' -and ($FocusOffsetXPercent -ne 0 -or $FocusOffsetYPercent -ne 0)) {throw 'Focus offsets require quad mode'}
$gameExecutable = Join-Path $GameDirectory 'Le Mans Ultimate.exe'
$expectedHash = '66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF'
if ((Get-FileHash -LiteralPath $gameExecutable -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'LMU executable version differs from reconstructed build'
}
if (Get-Process -Name 'Le Mans Ultimate' -ErrorAction SilentlyContinue) {
    throw 'LMU is already running. Close it before starting this diagnostic instance.'
}
$manifestRoot = $PSScriptRoot
$logDirectory = Join-Path $PSScriptRoot $(if($DisableDiagnostics){'runtime'}else{'logs'})
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$stamp = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,6)
$layerPlan=New-LmuLayerPlan -Backend $Backend -ManifestRoot $manifestRoot -QuadViewsManifest $QuadViewsManifest -RunDirectory (Join-Path $logDirectory "layers-$stamp")
$logPath = Join-Path $logDirectory "$Mode-$stamp.log"
$captureDirectory = $null
$captureTrigger = $null
$timingTrigger = $null
$benchmarkControl = $null
if ($Benchmark) { $benchmarkControl = Join-Path $logDirectory "benchmark-$Mode-$stamp.control" }
if ($GpuTiming) {
    $timingTrigger = Join-Path $logDirectory "timing-$Mode-$stamp.request"
    if (Test-Path -LiteralPath $timingTrigger) { throw 'Timing trigger already exists' }
}
if ($CaptureViews) {
    if ($Mode -ne 'quad') { throw 'Offscreen view capture requires quad mode' }
    $captureDirectory = Join-Path $logDirectory "views-$stamp"
    if (Test-Path -LiteralPath $captureDirectory) { throw 'Capture directory already exists' }
    New-Item -ItemType Directory -Path $captureDirectory | Out-Null
    $captureTrigger = Join-Path $captureDirectory 'capture.request'
}
$info = [System.Diagnostics.ProcessStartInfo]::new()
# Clear inherited mod controls before writing this launch's complete configuration.
foreach($key in @($info.Environment.Keys)) { if($key.StartsWith('LMU_QV_', [StringComparison]::OrdinalIgnoreCase)) {$info.Environment.Remove($key)|Out-Null} }
$info.FileName = $gameExecutable
$info.Arguments = '+XR'
$info.WorkingDirectory = $GameDirectory
$info.UseShellExecute = $false
$info.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
$info.Environment['XR_API_LAYER_PATH'] = $layerPlan.Paths
$info.Environment['XR_ENABLE_API_LAYERS'] = $layerPlan.Layers
if ($layerPlan.DisableVariable) { $info.Environment[$layerPlan.DisableVariable]='1' }
foreach($variable in $layerPlan.DisableVariables){$info.Environment[$variable]='1'}
# Its upstream stereo crop misaligns native outer and provider focus geometry.
if($Mode -eq 'quad'){$info.Environment['DISABLE_OPENGRIDXR']='1'}
$info.Environment['LMU_QV_FOVEATION_MODE'] = $FoveationMode
$info.Environment['LMU_QV_MODE'] = $Mode
$info.Environment['LMU_QV_VECTORXR_DIRECT_INPUTS'] = $(if ($Backend -eq 'VectorXR' -and $Mode -eq 'quad') { '1' } else { '0' })
$info.Environment['LMU_QV_PROFILE'] = $(if ($RenderProfiling) { '1' } else { '0' })
$info.Environment['LMU_QV_DIAGNOSTICS'] = $(if ($DisableDiagnostics) { '0' } else { '1' })
$info.Environment['LMU_QV_STEREO_AUDIT'] = $(if ($StereoDrawAudit) { '1' } else { '0' })
$info.Environment['LMU_QV_STEREO_AUDIT_DIR'] = $(if ($StereoDrawAudit) { $logDirectory } else { '' })
$info.Environment['LMU_QV_STEREO_SHADER_CAPTURE'] = $(if ($StereoDrawAudit -and $StereoShaderCapture) { '1' } else { '0' })
$info.Environment['LMU_QV_STEREO_DRAW_PROOF'] = $(if ($StereoDrawAudit -and $StereoPairProof) { '1' } else { '0' })
$info.Environment['LMU_QV_CPU_UPLOAD_REUSE'] = $(if ($CpuUploadReuse) { '1' } else { '0' })
$info.Environment['LMU_QV_CPU_OUTER_REUSE'] = $(if ($CpuOuterReuse) { '1' } else { '0' })
$info.Environment['LMU_QV_PROFILE_SECONDS'] = $RenderProfileSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_USE_RUNTIME_SETTINGS'] = $(if ($UseRuntimeSettings) { '1' } else { '0' })
$info.Environment['LMU_QV_LOG'] = $(if($DisableDiagnostics){''}else{$logPath})
$info.Environment['LMU_QV_PIXEL_STATS'] = $(if ($ScenePixelStats -or $CaptureViews) { 'scene' } elseif ($PixelStats) { '1' } else { '0' })
$info.Environment['LMU_QV_CAPTURE_DIR'] = $(if ($CaptureViews) { $captureDirectory } else { '' })
$info.Environment['LMU_QV_CAPTURE_TRIGGER'] = $(if ($CaptureViews) { $captureTrigger } else { '' })
$info.Environment['LMU_QV_GPU_TIMING_TRIGGER'] = $(if ($GpuTiming) { $timingTrigger } else { '' })
$info.Environment['LMU_QV_DEBUG_FOCUS'] = $(if ($DebugFocus) { '1' } else { '0' })
$info.Environment['LMU_QV_BENCH_CONTROL'] = $(if ($Benchmark) { $benchmarkControl } else { '' })
$info.Environment['LMU_QV_VRS_EXCLUSIVE'] = $(if ($EnableOuterVrs) { '1' } else { '0' })
$info.Environment['LMU_QV_NATIVE_OUTER'] = $(if ($NativeOuterResolution) { '1' } else { '0' })
$info.Environment['LMU_QV_OUTER_SSR'] = $(if ($OuterSsr) { '1' } else { '0' })
$info.Environment['LMU_QV_OUTER_SHADING_RATE'] = $OuterShadingRate.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_OUTER_PIXELS_PERCENT'] = $OuterPixelsPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_PIXELS_PERCENT'] = $FocusPixelsPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_BOX_PERCENT'] = $FocusBoxPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_WIDTH_PERCENT'] = $FocusWidthPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_HEIGHT_PERCENT'] = $FocusHeightPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_OFFSET_X_PERCENT'] = $FocusOffsetXPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
$info.Environment['LMU_QV_FOCUS_OFFSET_Y_PERCENT'] = $FocusOffsetYPercent.ToString([Globalization.CultureInfo]::InvariantCulture)
# Direct executable is the officially supported offline launch without EAC.
$gameProcess = [System.Diagnostics.Process]::Start($info)
$run = [pscustomobject]@{
    Pid=$gameProcess.Id;Mode=$Mode;PixelStats=[bool]$PixelStats;ScenePixelStats=[bool]$ScenePixelStats;Log=$logPath;Exe=$gameExecutable
    ExeSHA256=$expectedHash
    LayerSHA256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'build/lmu_quadview.dll') -Algorithm SHA256).Hash
    CaptureDirectory=$captureDirectory;CaptureTrigger=$captureTrigger
    GpuTiming=[bool]$GpuTiming;TimingTrigger=$timingTrigger
    RenderProfiling=[bool]$RenderProfiling;RenderProfileSeconds=$RenderProfileSeconds
    StereoDrawAudit=[bool]$StereoDrawAudit
    StereoShaderCapture=[bool]($StereoDrawAudit -and $StereoShaderCapture)
    StereoPairProof=[bool]($StereoDrawAudit -and $StereoPairProof)
    CpuUploadReuse=[bool]$CpuUploadReuse
    CpuOuterReuse=[bool]$CpuOuterReuse
    DebugFocus=[bool]$DebugFocus
    Benchmark=[bool]$Benchmark;BenchmarkControl=$benchmarkControl;EnableOuterVrs=[bool]$EnableOuterVrs
    NativeOuterResolution=[bool]$NativeOuterResolution
    OuterSsr=[bool]$OuterSsr
    UseRuntimeSettings=[bool]$UseRuntimeSettings
    Backend=$Backend;FoveationMode=$FoveationMode;ProviderManifest=$layerPlan.ProviderManifest;ProviderName=$layerPlan.ProviderName;ExplicitManifest=$layerPlan.ExplicitManifest
    DisabledImplicitSwitches=$layerPlan.DisableVariables
    OpenGridXrDisabled=($info.Environment['DISABLE_OPENGRIDXR'] -eq '1')
    EnabledLayers=$layerPlan.Layers
    OuterShadingRate=$OuterShadingRate
    OuterPixelsPercent=$OuterPixelsPercent;FocusPixelsPercent=$FocusPixelsPercent
    FocusBoxPercent=$FocusBoxPercent
    FocusWidthPercent=$FocusWidthPercent;FocusHeightPercent=$FocusHeightPercent
    FocusOffsetXPercent=$FocusOffsetXPercent;FocusOffsetYPercent=$FocusOffsetYPercent
    StartedUTC=[DateTime]::UtcNow.ToString('o')
}
if(!$DisableDiagnostics){$run | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory "$Mode-$stamp-run.json") -Encoding utf8}
$run | ConvertTo-Json
