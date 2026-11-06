param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../dist'),
    [string]$MakeNsisPath = '',
    [switch]$RequireSigned,
    [switch]$AllowUnsigned
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'version.ps1')
$version = Get-LmuVersion
if ($RequireSigned -and $AllowUnsigned) { throw 'RequireSigned and AllowUnsigned are mutually exclusive.' }
# Release inputs are signed by default. Setup itself is signed after compilation.
$requirePayloadSignature = !$AllowUnsigned
$stage = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\')
if (!(Test-Path -LiteralPath $stage -PathType Container)) { throw "Portable package directory does not exist: $stage" }
if ((Get-Item -LiteralPath $stage).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package directory must not be a reparse point.' }
$all = @(Get-ChildItem -LiteralPath $stage -Recurse -Force)
if (@($all | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Package must not contain reparse points.' }
if ((Get-Content -LiteralPath (Join-Path $stage 'VERSION') -Raw).Trim() -cne $version) { throw 'Package VERSION does not match repository VERSION.' }
$manifestPath = Join-Path $stage 'SHA256.txt'
$owned = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw 'Invalid portable SHA256.txt entry.' }
    $hash = $Matches[1]; $relative = $Matches[2].Replace('/', '\')
    # These paths are always user data, even if accidentally listed by a staging job.
    if ($relative -match '(^|\\)(\.\.?|launcher-settings\.xml|profiles|logs|runtime|Uninstall\.exe|installer-files\.txt)(\\|$)' -or
        $relative -match '[:"$*?\r\n]' -or [IO.Path]::IsPathRooted($relative) -or $relative.EndsWith('\') -or
        @($relative.Split('\') | Where-Object { !$_ -or $_.EndsWith('.') -or $_.EndsWith(' ') }).Count) { throw "Unsafe or user-data package path: $relative" }
    if ($owned.ContainsKey($relative)) { throw "Duplicate package path: $relative" }
    $source = Join-Path $stage $relative
    if (!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -cne $hash.ToUpperInvariant()) { throw "Package hash mismatch: $relative" }
    $owned[$relative] = $source
}
foreach ($required in @('VERSION','README.txt','LICENSE.md','LMU-QuadView-Launcher.exe','LMU-QuadView-Launcher.exe.config','build\lmu_quadview.dll','build\lmu_autostart.dll','tools\PortableStereoCheck.exe','XR_APILAYER_LMU_quadview.json')) {
    if (!$owned.ContainsKey($required)) { throw "Incomplete portable package: missing $required" }
}
foreach ($file in $all | Where-Object { !$_.PSIsContainer }) {
    $relative = $file.FullName.Substring($stage.Length + 1)
    if ($relative -ine 'SHA256.txt' -and !$owned.ContainsKey($relative)) { throw "Unmanifested package file: $relative" }
}
$owned['SHA256.txt'] = $manifestPath
$publisher = $null
foreach ($relative in @($owned.Keys | Where-Object { $_ -match '\.(dll|exe)$' } | Sort-Object)) {
    if ($relative -ine 'tools\PortableStereoCheck.exe') {
        $metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($owned[$relative])
        if ($metadata.ProductVersion -cne $version -or $metadata.FileVersion -cne ($version + '.0')) { throw "Binary version does not match VERSION: $relative" }
    }
    if ($requirePayloadSignature) {
        $signature = Get-AuthenticodeSignature -LiteralPath $owned[$relative]
        if ($signature.Status -ne 'Valid' -or !$signature.TimeStamperCertificate) { throw "Valid timestamped signature required: $relative" }
        if (!$publisher) { $publisher = $signature.SignerCertificate.Thumbprint }
        if ($signature.SignerCertificate.Thumbprint -ne $publisher) { throw "Release binaries must have the same signer: $relative" }
    }
}
if (!$MakeNsisPath) {
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { $MakeNsisPath = $command.Source }
    else {
        $cache = Join-Path $env:LOCALAPPDATA 'electron-builder/Cache'
        $candidate = Get-ChildItem -Path (Join-Path $cache 'nsis-*/nsis-*/Bin/makensis.exe') -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($candidate) { $MakeNsisPath = $candidate.FullName }
    }
}
if (!$MakeNsisPath -or !(Test-Path -LiteralPath $MakeNsisPath -PathType Leaf)) { throw 'NSIS Unicode compiler not found. Pass -MakeNsisPath pointing to an existing makensis.exe.' }
$MakeNsisPath = [IO.Path]::GetFullPath($MakeNsisPath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$setup = Join-Path $OutputDirectory ("LMU-Quad-View-Mod-$version-Setup-x64.exe")
if (Test-Path -LiteralPath $setup) { throw "Installer already exists: $setup" }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$work = Join-Path $OutputDirectory ('installer-build-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
function Quote-Nsis([string]$value) {
    if ($value -match '["\r\n]') { throw 'NSIS path contains an unsupported character.' }
    return '"' + $value.Replace('$', '$$') + '"'
}
$paths = @($owned.Keys | Sort-Object)
$directories = @($paths | ForEach-Object { $directory = [IO.Path]::GetDirectoryName($_); while ($directory) { $directory; $directory = [IO.Path]::GetDirectoryName($directory) } } | Sort-Object -Unique)
$install = @('!macro InstallPayload')
foreach ($directory in @('') + $directories) {
    $install += '  Push ' + (Quote-Nsis ('$INSTDIR' + $(if ($directory) { '\' + $directory } else { '' }))).Replace('$$INSTDIR', '$INSTDIR')
    $install += '  Call CheckPlainDirectory'
}
foreach ($relative in $paths + @('Uninstall.exe', 'installer-files.txt')) {
    $install += '  Push ' + (Quote-Nsis ('$INSTDIR\' + $relative)).Replace('$$INSTDIR', '$INSTDIR')
    $install += '  Call CheckPlainDirectory'
}
foreach ($relative in $paths) {
    $directory = [IO.Path]::GetDirectoryName($relative)
    $target = '$INSTDIR' + $(if ($directory) { '\' + $directory } else { '' })
    $install += '  SetOutPath ' + (Quote-Nsis $target).Replace('$$INSTDIR', '$INSTDIR')
    $install += '  File ' + (Quote-Nsis ('/oname=' + [IO.Path]::GetFileName($relative))) + ' ' + (Quote-Nsis $owned[$relative])
}
$paths | Set-Content -LiteralPath (Join-Path $work 'installer-files.txt') -Encoding UTF8
$install += '  SetOutPath "$INSTDIR"'
$install += '  File /oname=installer-files.txt ' + (Quote-Nsis (Join-Path $work 'installer-files.txt'))
$install += '!macroend'
$install += '!macro RemovePayload'
foreach ($directory in @('') + $directories) {
    $install += '  Push ' + (Quote-Nsis ('$INSTDIR' + $(if ($directory) { '\' + $directory } else { '' }))).Replace('$$INSTDIR', '$INSTDIR')
    $install += '  Call un.CheckPlainDirectory'
}
foreach ($relative in $paths) { $install += '  Delete ' + (Quote-Nsis ('$INSTDIR\' + $relative)).Replace('$$INSTDIR', '$INSTDIR') }
$install += '  Delete "$INSTDIR\installer-files.txt"'
foreach ($directory in @($directories | Sort-Object { $_.Length } -Descending)) { $install += '  RMDir ' + (Quote-Nsis ('$INSTDIR\' + $directory)).Replace('$$INSTDIR', '$INSTDIR') }
$install += '!macroend'
$include = Join-Path $work 'payload.nsh'
$install | Set-Content -LiteralPath $include -Encoding UTF8
& $MakeNsisPath '/V3' "/DAPP_VERSION=$version" "/DOUTPUT_FILE=$setup" "/DPAYLOAD_INCLUDE=$include" (Join-Path $PSScriptRoot 'installer.nsi')
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $setup)) { throw "NSIS compilation failed ($LASTEXITCODE). Build inputs retained: $work" }
Write-Output "Installer: $setup"
Write-Output "Compiler: $MakeNsisPath"
Write-Output "Ownership manifest and NSIS source: $work"
if ($AllowUnsigned) { Write-Warning 'TEST BUILD: payload signatures were not required. Do not publish this installer.' }
else { Write-Output 'Release payload signatures verified. Sign and timestamp the final setup EXE before publication.' }
