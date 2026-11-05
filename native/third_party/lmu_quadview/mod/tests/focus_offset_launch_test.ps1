$ErrorActionPreference='Stop'
$source=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../launch.ps1')
$prefix=$source.Substring(0,$source.IndexOf('$gameExecutable =')) -replace '(?m)^\. \(Join-Path \$PSScriptRoot ''portable-launch.ps1''\)\r?$',''
$environment=$source.Substring($source.IndexOf('$info = '),$source.IndexOf('# Direct executable')-$source.IndexOf('$info = '))
$metadata=$source.Substring($source.IndexOf('$run = '),$source.IndexOf('if(!$DisableDiagnostics)')-$source.IndexOf('$run = '))
$setup=@'
$PSScriptRoot='C:\fixture';$gameExecutable='C:\fixture\Le Mans Ultimate.exe';$logPath='fixture.log';$logDirectory='C:\fixture';$expectedHash='fixture'
$layerPlan=[pscustomobject]@{Paths='fixture';Layers='fixture';DisableVariables=@()}
function Get-FileHash { [pscustomobject]@{Hash='fixture'} }
'@
$fixture=[scriptblock]::Create($prefix+$setup+[Environment]::NewLine+$environment+$metadata+';[pscustomobject]@{Info=$info;Run=($run|ConvertTo-Json|ConvertFrom-Json)}')
$old=$env:LMU_QV_FOCUS_OFFSET_X_PERCENT
try {
    $env:LMU_QV_FOCUS_OFFSET_X_PERCENT='49'
    foreach($provider in @('Native','QVFR','VectorXR')) {
        $result=& $fixture -Mode quad -Backend $provider -UseRuntimeSettings -FocusOffsetXPercent -12.5 -FocusOffsetYPercent 50
        if($result.Info.Environment['LMU_QV_FOCUS_OFFSET_X_PERCENT'] -ne '-12.5' -or $result.Info.Environment['LMU_QV_FOCUS_OFFSET_Y_PERCENT'] -ne '50') {throw 'CLI signed invariant offset environment missing'}
        if($result.Run.FocusOffsetXPercent -ne -12.5 -or $result.Run.FocusOffsetYPercent -ne 50) {throw 'CLI offset metadata missing'}
    }
    $small=& $fixture -Mode quad -UseRuntimeSettings -FocusOffsetXPercent 0.00001 -FocusOffsetYPercent -0.000025
    if($small.Info.Environment['LMU_QV_FOCUS_OFFSET_X_PERCENT'] -ne '1E-05' -or $small.Info.Environment['LMU_QV_FOCUS_OFFSET_Y_PERCENT'] -ne '-2.5E-05') {throw 'CLI small offset scientific output changed'}
    if($small.Run.FocusOffsetXPercent -ne 0.00001 -or $small.Run.FocusOffsetYPercent -ne -0.000025) {throw 'CLI small offset metadata lost precision'}
    $default=& $fixture -Mode quad
    if($default.Info.Environment['LMU_QV_FOCUS_OFFSET_X_PERCENT'] -ne '0') {throw 'CLI stale inherited offset survived defaults'}
    foreach($case in @(@{Mode='trace';FocusOffsetXPercent=1},@{Mode='stereo-timing';FocusOffsetYPercent=-1},@{Mode='quad';FocusOffsetXPercent=[double]::NaN},@{Mode='quad';FocusOffsetYPercent=[double]::PositiveInfinity},@{Mode='quad';FocusOffsetXPercent=50.01},@{Mode='quad';FocusOffsetYPercent=-50.01})) {
        $rejected=$false
        try {& $fixture @case|Out-Null} catch {$rejected=$true}
        if(!$rejected) {throw ('CLI accepted invalid offset/mode: '+($case|ConvertTo-Json -Compress))}
    }
    'CLI configuration tests passed: three providers, runtime settings, signed environment/metadata, stale clearing, invalid values/modes; process-launch statement excluded.'
} finally {$env:LMU_QV_FOCUS_OFFSET_X_PERCENT=$old}
