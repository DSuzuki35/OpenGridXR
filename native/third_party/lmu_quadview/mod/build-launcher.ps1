param([string]$OutputPath = (Join-Path $PSScriptRoot 'build/LMU-QuadView-Launcher.exe'))
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 C# compiler not found' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
. (Join-Path $PSScriptRoot '../tools/version.ps1')
$versionFiles = Write-LmuVersionFiles (Join-Path $PSScriptRoot 'build/launcher-version')
$core = @((Join-Path $PSScriptRoot 'launcher/QvfrSettings.cs'),(Join-Path $PSScriptRoot 'launcher/FocusGeometry.cs'),(Join-Path $PSScriptRoot 'launcher/LaunchService.cs'),(Join-Path $PSScriptRoot 'launcher/AssemblyInfo.cs'))
$manifest = Join-Path $PSScriptRoot 'launcher/app.manifest'
$core += $versionFiles.AssemblyPath
$core += @('BenchmarkController.cs','JoystickInput.cs','LaunchDiagnostics.cs') | ForEach-Object { Join-Path $PSScriptRoot ('launcher/' + $_) }
$icon = Join-Path $PSScriptRoot 'launcher/assets/quadview.ico'
$appConfig = Join-Path $PSScriptRoot 'launcher/app.config'
$references = @('/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Xml.Linq.dll','/reference:System.Web.Extensions.dll')
foreach ($test in @('SettingsTests','FocusGeometryTests','LaunchServiceTests','JoystickInputTests','LaunchDiagnosticsTests','BenchmarkTests')) {
    $testOutput = Join-Path $PSScriptRoot ('build/' + $test + '.exe')
    & $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ "/main:$test" "/out:$testOutput" @references @core (Join-Path $PSScriptRoot "launcher/$test.cs")
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
    if ($test -eq 'BenchmarkTests') { & $testOutput (Join-Path $PSScriptRoot 'build/benchmark-qa') }
    else { & $testOutput }
    if ($LASTEXITCODE -ne 0) { throw "$test failed" }
}
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /warnaserror+ "/out:$OutputPath" "/win32manifest:$manifest" "/win32icon:$icon" "/resource:$icon,LmuQuadView.Icon" @references @core (Join-Path $PSScriptRoot 'launcher/Launcher.cs')
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed' }
Copy-Item -LiteralPath $appConfig -Destination ($OutputPath + '.config')
$layoutTest = Join-Path $PSScriptRoot 'build/GuiLayoutTests.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ /main:GuiLayoutTests "/out:$layoutTest" "/win32manifest:$manifest" "/win32icon:$icon" "/resource:$icon,LmuQuadView.Icon" @references @core (Join-Path $PSScriptRoot 'launcher/Launcher.cs') (Join-Path $PSScriptRoot 'launcher/GuiLayoutTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'GUI layout test compilation failed' }
Copy-Item -LiteralPath $appConfig -Destination ($layoutTest + '.config')
& $layoutTest (Join-Path $PSScriptRoot 'build/gui-qa')
if ($LASTEXITCODE -ne 0) { throw 'GUI layout tests failed' }
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
