param(
    [string]$SimHubPath = 'C:\Program Files (x86)\SimHub',
    [string]$OutputDirectory = '',
    [switch]$TestOnly
)

$ErrorActionPreference = 'Stop'
$workspacePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $workspacePath '.local\simhub-build'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$SimHubPath = [IO.Path]::GetFullPath($SimHubPath).TrimEnd('\')
if ($OutputDirectory.TrimEnd('\').Equals($SimHubPath, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputDirectory.StartsWith($SimHubPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be outside the SimHub installation. This helper only builds; installation is separate.'
}
$frameworkPath = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compilerPath = Join-Path $frameworkPath 'csc.exe'
if (!(Test-Path -LiteralPath $compilerPath)) {
    throw 'The Windows .NET Framework 4.x C# compiler is required.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$sourcePath = Join-Path $workspacePath 'integrations\simhub'
$coreSource = Join-Path $sourcePath 'oVRlay.LedForwarder.cs'
$senderSource = Join-Path $sourcePath 'oVRlay.LocalUdpSender.cs'
$testSource = Join-Path $sourcePath 'oVRlay.SimHubLeds.Tests.cs'
$testExe = Join-Path $OutputDirectory 'oVRlay.SimHubLeds.Tests.exe'
& $compilerPath /nologo /optimize+ /warnaserror+ /target:exe "/out:$testExe" /r:System.Drawing.dll $coreSource $testSource
if ($LASTEXITCODE -ne 0) { throw "LED tests could not compile (exit $LASTEXITCODE)." }
& $testExe
if ($LASTEXITCODE -ne 0) { throw "LED tests failed (exit $LASTEXITCODE)." }
if ($TestOnly) { return }

$referenceNames = @('SimHub.Plugins.dll', 'GameReaderCommon.dll', 'BA63Driver.dll', 'WoteverCommon.dll', 'Newtonsoft.Json.dll')
$references = @()
foreach ($name in $referenceNames) {
    $referencePath = Join-Path $SimHubPath $name
    if (!(Test-Path -LiteralPath $referencePath)) { throw "Missing SimHub runtime reference: $referencePath" }
    $references += "/r:$referencePath"
}
foreach ($name in @('PresentationCore.dll', 'PresentationFramework.dll', 'WindowsBase.dll')) {
    $referencePath = Join-Path (Join-Path $frameworkPath 'WPF') $name
    if (!(Test-Path -LiteralPath $referencePath)) { $referencePath = Join-Path $frameworkPath $name }
    if (!(Test-Path -LiteralPath $referencePath)) { throw "Missing Windows WPF reference: $referencePath" }
    $references += "/r:$referencePath"
}
$pluginPath = Join-Path $OutputDirectory 'oVRlay.SimHubLeds.dll'
$pluginSource = Join-Path $sourcePath 'oVRlay.SimHubLeds.cs'
& $compilerPath /nologo /optimize+ /warnaserror+ /target:library "/out:$pluginPath" "/r:System.Core.dll" "/r:System.Drawing.dll" "/r:System.Xaml.dll" @references $coreSource $senderSource $pluginSource
if ($LASTEXITCODE -ne 0) { throw "SimHub observer build failed (exit $LASTEXITCODE)." }

$manifest = [ordered]@{
    BuiltAtUtc = [DateTime]::UtcNow.ToString('o')
    Compiler = $compilerPath
    SimHubPath = $SimHubPath
    Plugin = $pluginPath
    PluginSha256 = (Get-FileHash -LiteralPath $pluginPath -Algorithm SHA256).Hash
    References = @($referenceNames | ForEach-Object {
        $referencePath = Join-Path $SimHubPath $_
        [ordered]@{ File = $_; Sha256 = (Get-FileHash -LiteralPath $referencePath -Algorithm SHA256).Hash }
    })
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'oVRlay.SimHubLeds.build.json') -Encoding UTF8
Write-Output "Built: $pluginPath"
Write-Output 'Build only: no files were copied into SimHub and no applications were restarted.'
