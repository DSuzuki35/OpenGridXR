param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$SkipEnhancements)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake 3.24+ and Visual Studio 2022 Desktop C++ tools are required.' }
if (-not $SkipEnhancements) { & (Join-Path $PSScriptRoot 'build-enhancements.ps1') -Configuration $Configuration }
& $cmake -S (Join-Path $repo 'native') -B (Join-Path $repo 'native\build') -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'Native configure failed.' }
& $cmake --build (Join-Path $repo 'native\build') --config $Configuration
if ($LASTEXITCODE) { throw 'Native build failed.' }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
& $ctest --test-dir (Join-Path $repo 'native\build') -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Native tests failed.' }
