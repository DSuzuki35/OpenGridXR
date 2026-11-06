param([string]$OutputPath = (Join-Path $PSScriptRoot 'build/LMU-QuadView-Launcher.exe'))
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework x64 C# compiler not found' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
. (Join-Path $PSScriptRoot '../tools/version.ps1')
$versionFiles = Write-LmuVersionFiles (Join-Path $PSScriptRoot 'build/launcher-version')
$core = @((Join-Path $PSScriptRoot 'launcher/QvfrSettings.cs'),(Join-Path $PSScriptRoot 'launcher/SettingRanges.cs'),(Join-Path $PSScriptRoot 'launcher/FocusGeometry.cs'),(Join-Path $PSScriptRoot 'launcher/LaunchService.cs'),(Join-Path $PSScriptRoot 'launcher/AssemblyInfo.cs'))
$ui = @('Launcher.cs','NativeChrome.cs','LauncherUpdates.cs') | ForEach-Object { Join-Path $PSScriptRoot ('launcher/' + $_) }
$manifest = Join-Path $PSScriptRoot 'launcher/app.manifest'
$core += $versionFiles.AssemblyPath
$core += @('BenchmarkController.cs','JoystickInput.cs','LaunchDiagnostics.cs','SteamLaunch.cs','AutoStart.cs','RegistryRestoreScope.cs','ProfileStore.cs','UpdateService.cs','ExternalCommands.cs') | ForEach-Object { Join-Path $PSScriptRoot ('launcher/' + $_) }
$icon = Join-Path $PSScriptRoot 'launcher/assets/quadview.ico'
$appConfig = Join-Path $PSScriptRoot 'launcher/app.config'
$references = @('/reference:System.Windows.Forms.dll','/reference:System.Drawing.dll','/reference:System.Xml.Linq.dll','/reference:System.Web.Extensions.dll')
foreach ($test in @('ExternalCommandsTests','PortableChecksTests','UpdateServiceTests','ProfileStoreTests','RegistrySafetyTests','HardeningTests','SettingsTests','FocusGeometryTests','LaunchServiceTests','SteamLaunchTests','AutoStartTests','RegistryOwnerTests','ProcessPathTests','JoystickInputTests','LaunchDiagnosticsTests','BenchmarkTests')) {
    $testOutput = Join-Path $PSScriptRoot ('build/' + $test + '.exe')
    & $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ "/main:$test" "/out:$testOutput" @references @core (Join-Path $PSScriptRoot "launcher/$test.cs")
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
    if ($test -eq 'RegistryOwnerTests') { continue } # Explicit elevated fixture, never edits production OpenXR keys.
    if ($test -eq 'BenchmarkTests') { & $testOutput (Join-Path $PSScriptRoot 'build/benchmark-qa') }
    else { & $testOutput }
    if ($LASTEXITCODE -ne 0) { throw "$test failed" }
}
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /warnaserror+ "/out:$OutputPath" "/win32manifest:$manifest" "/win32icon:$icon" "/resource:$icon,LmuQuadView.Icon" @references @core @ui
if ($LASTEXITCODE -ne 0) { throw 'Launcher compilation failed' }
& (Join-Path $PSScriptRoot 'build/RegistrySafetyTests.exe') $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'Launcher contains an unsafe registry rename import' }
Copy-Item -LiteralPath $appConfig -Destination ($OutputPath + '.config')
$layoutTest = Join-Path $PSScriptRoot 'build/GuiLayoutTests.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ /main:GuiLayoutTests "/out:$layoutTest" "/win32manifest:$manifest" "/win32icon:$icon" "/resource:$icon,LmuQuadView.Icon" @references @core @ui (Join-Path $PSScriptRoot 'launcher/GuiLayoutTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'GUI layout test compilation failed' }
Copy-Item -LiteralPath $appConfig -Destination ($layoutTest + '.config')
& $layoutTest (Join-Path $PSScriptRoot 'build/gui-qa')
if ($LASTEXITCODE -ne 0) { throw 'GUI layout tests failed' }
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
& $compiler /nologo /target:exe /platform:x64 /optimize+ /warnaserror+ /main:SliderRangeTests "/out:$(Join-Path $PSScriptRoot 'build/SliderRangeTests.exe')" @references @core @ui (Join-Path $PSScriptRoot 'launcher/SliderRangeTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Slider range tests compilation failed' }
& (Join-Path $PSScriptRoot 'build/SliderRangeTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Slider range tests failed' }
