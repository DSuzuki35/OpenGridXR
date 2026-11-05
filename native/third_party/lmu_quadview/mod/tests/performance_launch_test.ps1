$ErrorActionPreference='Stop'
# Execute the launcher's validation, child environment and metadata; omit all
# filesystem/provider setup and the actual game process launch.
$source=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../launch.ps1')
$prefix=$source.Substring(0,$source.IndexOf('$gameExecutable =')) -replace '(?m)^\. \(Join-Path \$PSScriptRoot ''portable-launch.ps1''\)\r?$',''
$environment=$source.Substring($source.IndexOf('$info = '),$source.IndexOf('# Direct executable')-$source.IndexOf('$info = '))
$metadata=$source.Substring($source.IndexOf('$run = '),$source.IndexOf('if(!$DisableDiagnostics)')-$source.IndexOf('$run = '))
$setup=@'
$PSScriptRoot='C:\fixture';$gameExecutable='C:\fixture\Le Mans Ultimate.exe';$logPath='fixture.log';$logDirectory='C:\fixture';$expectedHash='fixture'
$layerPlan=[pscustomobject]@{Paths='fixture';Layers='fixture';DisableVariable=$null;DisableVariables=@();ProviderManifest=$null;ProviderName='fixture';ExplicitManifest=$null}
$gameProcess=[pscustomobject]@{Id=123}
function Get-FileHash { [pscustomobject]@{Hash='fixture'} }
'@
$fixture=[scriptblock]::Create($prefix+$setup+[Environment]::NewLine+$environment+$metadata+';[pscustomobject]@{Info=$info;Run=($run|ConvertTo-Json|ConvertFrom-Json)}')
$keys=@('LMU_QV_OUTER_SSR','LMU_QV_CPU_OUTER_REUSE','LMU_QV_CPU_UPLOAD_REUSE')
$saved=@{}
foreach($key in $keys){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
try {
    $savedGrid=[Environment]::GetEnvironmentVariable('DISABLE_OPENGRIDXR','Process')
    try {
        [Environment]::SetEnvironmentVariable('DISABLE_OPENGRIDXR',$null,'Process')
        foreach($mode in @('quad','trace','stereo-timing')) {
            $grid=& $fixture -Mode $mode
            $actual=$grid.Info.Environment['DISABLE_OPENGRIDXR']
            if(($mode -eq 'quad' -and $actual -ne '1') -or ($mode -ne 'quad' -and ![string]::IsNullOrEmpty($actual))){throw "OpenGridXR isolation incorrect for $mode"}
            if([Environment]::GetEnvironmentVariable('DISABLE_OPENGRIDXR','Process')){throw 'OpenGridXR isolation changed parent'}
        }
    } finally {[Environment]::SetEnvironmentVariable('DISABLE_OPENGRIDXR',$savedGrid,'Process')}
    foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,'1','Process')}
    $default=& $fixture -Mode quad
    foreach($key in $keys){if($default.Info.Environment[$key] -ne '0'){throw "Inherited optimization survived defaults: $key"}}
    if($default.Run.OuterSsr -ne $false -or $default.Run.CpuOuterReuse -ne $false){throw 'Default performance metadata must be false'}
    foreach($provider in @('Native','QVFR','VectorXR')) {
        $result=& $fixture -Mode quad -Backend $provider -UseRuntimeSettings -NativeOuterResolution -OuterSsr -CpuOuterReuse -RenderProfiling
        foreach($key in $keys){if($result.Info.Environment[$key] -ne '1'){throw "Requested optimization missing for ${provider}: $key"}}
        if(!$result.Run.OuterSsr -or !$result.Run.CpuOuterReuse -or !$result.Run.CpuUploadReuse){throw 'Requested/effective optimization metadata missing'}
        if($result.Info.Environment['LMU_QV_PROFILE'] -ne '1' -or !$result.Run.RenderProfiling){throw 'Performance options disabled regular profiling'}
    }
    $ssr=& $fixture -Mode quad -NativeOuterResolution -OuterPixelsPercent 50 -OuterSsr
    if($ssr.Info.Environment['LMU_QV_OUTER_SSR'] -ne '1' -or $ssr.Info.Environment['LMU_QV_CPU_OUTER_REUSE'] -ne '0' -or $ssr.Info.Environment['LMU_QV_CPU_UPLOAD_REUSE'] -ne '0'){throw 'SSR unexpectedly enabled CPU reuse'}
    $cpu=& $fixture -Mode quad -CpuOuterReuse -CpuUploadReuse:$false
    if($cpu.Info.Environment['LMU_QV_CPU_OUTER_REUSE'] -ne '1' -or $cpu.Info.Environment['LMU_QV_CPU_UPLOAD_REUSE'] -ne '1' -or $cpu.Info.Environment['LMU_QV_OUTER_SSR'] -ne '0'){throw 'CPU outer reuse must imply upload reuse independently of SSR'}
    $quiet=& $fixture -Mode quad -UseRuntimeSettings -NativeOuterResolution -OuterSsr -CpuOuterReuse -RenderProfiling -DisableDiagnostics
    foreach($key in $keys){if($quiet.Info.Environment[$key] -ne '1'){throw "Diagnostics disabled optimization: $key"}}
    if($quiet.Info.Environment['LMU_QV_DIAGNOSTICS'] -ne '0' -or $quiet.Info.Environment['LMU_QV_PROFILE'] -ne '0' -or !$quiet.Run.OuterSsr -or !$quiet.Run.CpuOuterReuse){throw 'Diagnostics-off optimization metadata/configuration mismatch'}
    foreach($case in @(
        @{Mode='trace';OuterSsr=$true},
        @{Mode='stereo-timing';OuterSsr=$true},
        @{Mode='quad';OuterSsr=$true},
        @{Mode='trace';CpuOuterReuse=$true},
        @{Mode='stereo-timing';CpuOuterReuse=$true},
        @{Mode='quad';CpuOuterReuse=$true;StereoDrawAudit=$true},
        @{Mode='quad';CpuOuterReuse=$true;StereoPairProof=$true},
        @{Mode='quad';CpuOuterReuse=$true;StereoShaderCapture=$true}
    )) {
        $rejected=$false
        try {& $fixture @case|Out-Null} catch {$rejected=$true}
        if(!$rejected){throw ('Invalid performance configuration accepted: '+($case|ConvertTo-Json -Compress))}
    }
    'Performance CLI tests passed: three providers, opt-in defaults, stale clearing, environment/metadata, mode/dependency guards, profiling and diagnostics-off; no game launched.'
} finally {foreach($key in $keys){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
