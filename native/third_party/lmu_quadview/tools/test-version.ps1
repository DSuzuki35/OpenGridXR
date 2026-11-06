$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = Get-LmuVersion
$root = Join-Path $PSScriptRoot ('../mod/build/version-qa-' + [Guid]::NewGuid().ToString('N'))
$root = [IO.Path]::GetFullPath($root)
$msvc = Join-Path $root 'mod/build/msvc'
$gnu = Join-Path $root 'mod/build'
New-Item -ItemType Directory -Path $msvc -Force | Out-Null
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
function Fixture([string]$product, [string]$fileVersion, [string]$output) {
    $source = Join-Path $root 'Fixture.cs'
    @"
[assembly:System.Reflection.AssemblyInformationalVersion("$product")]
[assembly:System.Reflection.AssemblyFileVersion("$fileVersion")]
class Fixture { static void Main() {} }
"@ | Set-Content -LiteralPath $source -Encoding ASCII
    & $compiler /nologo "/out:$output" $source
    if ($LASTEXITCODE -ne 0) { throw 'Version fixture compilation failed.' }
}
$old = Join-Path $msvc 'nv_vrs_hardware_test.exe'
$current = Join-Path $gnu 'nv_vrs_hardware_test.exe'
Fixture '0.0.0' '0.0.0.0' $old
$rejected = $false
try { Find-LmuVrsDiagnostic $root | Out-Null } catch { $rejected = $true }
if (!$rejected) { throw 'Stale diagnostic was accepted.' }
Fixture $version ($version + '.0') $current
if ((Find-LmuVrsDiagnostic $root) -cne 'mod/build/nv_vrs_hardware_test.exe') { throw 'Stale MSVC output shadowed current GNU output.' }
Fixture $version '0.0.0.0' $old
if ((Find-LmuVrsDiagnostic $root) -cne 'mod/build/nv_vrs_hardware_test.exe') { throw 'Mismatched file version was accepted.' }
Fixture $version ($version + '.0') $old
if ((Find-LmuVrsDiagnostic $root) -cne 'mod/build/msvc/nv_vrs_hardware_test.exe') { throw 'Matching preferred diagnostic was skipped.' }
Write-Output 'Version selection passed: stale-only refusal, fallback to current build, file-version check, matching preferred build.'
