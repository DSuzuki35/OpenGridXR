param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Thumbprint,
    [string]$SignTool = '',
    [string]$TimestampUrl = 'http://time.certum.pl'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$SignTool) {
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $SignTool = Get-ChildItem -LiteralPath $sdk -Directory | Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64/signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$SignTool -or !(Test-Path -LiteralPath $SignTool)) { throw 'Windows SDK SignTool not found.' }
$cert = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $Thumbprint)
if (!$cert.HasPrivateKey -or $cert.NotAfter -le (Get-Date) -or $cert.NotBefore -gt (Get-Date)) { throw 'A current SimplySign code-signing certificate with its private-key provider is required.' }
$files = @('mod/build/lmu_quadview.dll','mod/build/lmu_autostart.dll','mod/build/LMU-QuadView-Launcher.exe') | ForEach-Object { Join-Path $repo $_ }
$proof = Join-Path $repo 'mod/build/msvc/stereo_instancing_test.exe'
if (!(Test-Path -LiteralPath $proof)) { $proof = Join-Path $repo 'mod/build/stereo_instancing_test.exe' }
$files += $proof
. (Join-Path $PSScriptRoot 'version.ps1')
$files += Join-Path $repo (Find-LmuVrsDiagnostic $repo)
foreach ($file in $files) { if (!(Test-Path -LiteralPath $file)) { throw "Build first: $file" } }
# SimplySign Desktop owns authentication and the cloud key. No PIN, token or
# private key is read, exported or stored by this script.
& $SignTool sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 /d 'LMU Quad View Mod' @files
if ($LASTEXITCODE -ne 0) { throw 'Signing failed. Connect SimplySign Desktop and retry; do not package an unsigned release.' }
foreach ($file in $files) {
    & $SignTool verify /pa /all /tw $file
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $file" }
    $signature = Get-AuthenticodeSignature -LiteralPath $file
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $Thumbprint -or !$signature.TimeStamperCertificate) { throw "Expected signer/timestamp missing: $file" }
    Write-Output "Signed and verified: $file"
}
