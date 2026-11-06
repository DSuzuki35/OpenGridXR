param([string]$PackageRoot=(Split-Path $PSScriptRoot -Parent),[switch]$PrepareOnly)
$ErrorActionPreference='Stop'
# Diagnostic launch uses the same executable/provider validation as the UI.
# It arms one four-view capture; the trigger is created separately after track loading.
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing,System.Xml.Linq,System.Web.Extensions
[Reflection.Assembly]::LoadFrom((Join-Path $PackageRoot 'LMU-QuadView-Launcher.exe')) | Out-Null
$settings=[LmuQuadViewLauncher.QvfrSettings]::Load((Join-Path $PackageRoot 'launcher-settings.xml'))
if($settings.Backend -eq 'VRS'){throw 'Four-view capture requires a quad-view backend.'}
$settings.DiagnosticsEnabled=$true
$info=[LmuQuadViewLauncher.LaunchService]::Prepare($PackageRoot,$settings)
$directory=Join-Path $PackageRoot ('logs/capture-'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $directory | Out-Null
$trigger=Join-Path $directory 'capture.trigger'
$info.EnvironmentVariables['LMU_QV_CAPTURE_DIR']=$directory
$info.EnvironmentVariables['LMU_QV_CAPTURE_TRIGGER']=$trigger
$capture=[ordered]@{Status='prepared';Pid=0;Directory=$directory;Trigger=$trigger;Log=$info.EnvironmentVariables['LMU_QV_LOG'];Backend=$settings.Backend;NativeOuterResolution=$settings.NativeOuterResolution;PreparedUTC=[DateTime]::UtcNow.ToString('o')}
$manifest=Join-Path $directory 'capture-run.json'
$capture | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
if($PrepareOnly){Write-Output "Validated capture launch, no game started. Manifest: $manifest";return}
if($settings.Backend -eq 'QVFR'){[LmuQuadViewLauncher.QvfrSettings]::SaveProfile($settings)}
try {
 $process=[Diagnostics.Process]::Start($info)
 if(!$process){throw 'LMU did not start.'}
 $capture.Status='started';$capture.Pid=$process.Id
 $capture | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
 Write-Output "LMU PID: $($process.Id)"
 Write-Output "Capture is armed. Once on track, create this empty trigger file: $trigger"
 Write-Output "Capture manifest: $manifest"
 $process.Dispose()
}catch{
 $capture.Status='failed';$capture['Error']=$_.Exception.Message
 $capture | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
 throw
}
