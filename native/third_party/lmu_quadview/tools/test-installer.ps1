param([string]$MakeNsisPath = '', [Parameter(Mandatory=$true)][string]$SevenZipPath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = Get-LmuVersion
$work = Join-Path $PSScriptRoot ('../dist/installer-tests-' + [Guid]::NewGuid().ToString('N'))
$work = [IO.Path]::GetFullPath($work)
$stage = Join-Path $work 'Portable package with spaces'
New-Item -ItemType Directory -Path $work,$stage,(Join-Path $stage 'build'),(Join-Path $stage 'tools') -Force | Out-Null
$source = Join-Path $work 'Fixture.cs'
@"
[assembly:System.Reflection.AssemblyFileVersion("$version.0")]
[assembly:System.Reflection.AssemblyInformationalVersion("$version")]
class Fixture { static void Main(string[] args) { if(args.Length>0) System.Threading.Thread.Sleep(int.Parse(args[0])); } }
"@ | Set-Content -LiteralPath $source
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$fixture = Join-Path $work 'Fixture.exe'
& $compiler /nologo "/out:$fixture" $source
if ($LASTEXITCODE -ne 0) { throw 'Could not build installer test fixture.' }
foreach ($name in @('LMU-QuadView-Launcher.exe','build/lmu_quadview.dll','build/lmu_autostart.dll','tools/PortableStereoCheck.exe')) { Copy-Item -LiteralPath $fixture -Destination (Join-Path $stage $name) }
foreach ($name in @('README.txt','LICENSE.md','LMU-QuadView-Launcher.exe.config','XR_APILAYER_LMU_quadview.json')) { 'installer fixture' | Set-Content -LiteralPath (Join-Path $stage $name) }
'<configuration />' | Set-Content -LiteralPath (Join-Path $stage 'LMU-QuadView-Launcher.exe.config')
$version | Set-Content -LiteralPath (Join-Path $stage 'VERSION')
$manifest = Join-Path $stage 'SHA256.txt'
Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash,$_.FullName.Substring($stage.Length+1).Replace('\','/')
} | Set-Content -LiteralPath $manifest
$manifestText = Get-Content -LiteralPath $manifest -Raw
function Build([string]$output, [bool]$unsigned) {
    $arguments = @{ PackageDirectory=$stage; OutputDirectory=$output; MakeNsisPath=$MakeNsisPath }
    if ($unsigned) { $arguments.AllowUnsigned=$true }
    & (Join-Path $PSScriptRoot 'build-installer.ps1') @arguments
}
function MustReject([string]$expected, [bool]$unsigned=$true) {
    try { Build (Join-Path $work ('Rejected ' + [Guid]::NewGuid().ToString('N'))) $unsigned | Out-Null }
    catch { if ($_.Exception.Message -notmatch $expected) { throw }; return }
    throw "Installer build unexpectedly accepted: $expected"
}
MustReject 'timestamped signature required' $false
'tampered' | Add-Content -LiteralPath (Join-Path $stage 'README.txt')
MustReject 'hash mismatch'
'installer fixture' | Set-Content -LiteralPath (Join-Path $stage 'README.txt')
Add-Content -LiteralPath $manifest -Value (('0' * 64) + '  ../escape.txt')
MustReject 'Unsafe or user-data'
$manifestText | Set-Content -LiteralPath $manifest -NoNewline
Add-Content -LiteralPath $manifest -Value (('0' * 64) + '  launcher-settings.xml')
MustReject 'Unsafe or user-data'
$manifestText | Set-Content -LiteralPath $manifest -NoNewline
'private data' | Set-Content -LiteralPath (Join-Path $stage 'unmanifested.txt')
MustReject 'Unmanifested package file'
Remove-Item -LiteralPath (Join-Path $stage 'unmanifested.txt')
$output = Join-Path $work 'Compiled setup with spaces'
Build $output $true
$setup = Join-Path $output ("LMU-Quad-View-Mod-$version-Setup-x64.exe")
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($setup)
if ($metadata.ProductVersion -cne $version -or $metadata.FileVersion -cne "$version.0") { throw 'Installer version metadata is incorrect.' }
$extracted = Join-Path $work 'Extracted payload'
& $SevenZipPath x -y "-o$extracted" $setup | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'NSIS payload extraction failed.' }
foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    $target = Join-Path $extracted $file.FullName.Substring($stage.Length+1)
    if (!(Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -cne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Extracted payload mismatch: $target" }
}
$payload = Get-ChildItem -LiteralPath $output -Filter payload.nsh -Recurse | Select-Object -First 1
$payloadText = Get-Content -LiteralPath $payload.FullName -Raw
if ($payloadText -match '(?im)^\s*(Delete|RMDir).*[*?]|^\s*RMDir\s+/r|^\s*Delete.*(launcher-settings\.xml|\\profiles\\|\\logs\\|\\runtime\\)') { throw 'Unsafe generated uninstall command.' }

function MustBlockBeforeMutation([string]$executable, [string]$arguments, [string]$target) {
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(20000)) { $process.Kill(); throw 'Guarded installer did not exit.' }
    $process.Refresh()
    if ($process.ExitCode -ne 2) { throw "Process guard returned $($process.ExitCode), expected 2." }
    if (Test-Path -LiteralPath $target) { throw 'Process guard allowed a package directory mutation.' }
}
# Run only failure paths: these checks never perform an installation or change app registration.
$launcher = $null; $game = $null
try {
    $launcher = Start-Process -FilePath (Join-Path $stage 'LMU-QuadView-Launcher.exe') -ArgumentList '30000' -WindowStyle Hidden -PassThru
    $target = Join-Path $work 'Must never install here'
    MustBlockBeforeMutation $setup "/S /D=$target" $target
    $launcher.Kill(); $launcher.WaitForExit(); $launcher = $null
    $gamePath = Join-Path $work 'Le Mans Ultimate.exe'
    Copy-Item -LiteralPath $fixture -Destination $gamePath
    $game = Start-Process -FilePath $gamePath -ArgumentList '30000' -WindowStyle Hidden -PassThru
    MustBlockBeforeMutation $setup "/S /D=$target" $target
    $launcher = Start-Process -FilePath (Join-Path $stage 'LMU-QuadView-Launcher.exe') -ArgumentList '1200' -WindowStyle Hidden -PassThru
    $timer = [Diagnostics.Stopwatch]::StartNew()
    MustBlockBeforeMutation $setup "/S /WAITPID=$($launcher.Id) /D=$target" $target
    if ($timer.ElapsedMilliseconds -lt 700) { throw 'WAITPID did not wait for the launcher to exit.' }
    $launcher.WaitForExit(); $launcher = $null
    $uninstaller = Join-Path $extracted 'Uninstall.exe'
    if (Test-Path -LiteralPath $uninstaller) { MustBlockBeforeMutation $uninstaller "/S _?=$target" $target }
} finally {
    foreach ($process in @($launcher,$game)) { if ($process -and !$process.HasExited) { $process.Kill(); $process.WaitForExit() } }
}
Write-Output "Installer validation, metadata, unpacked hashes, launcher/game guards, WAITPID and uninstall guard passed. Artifacts: $work"
