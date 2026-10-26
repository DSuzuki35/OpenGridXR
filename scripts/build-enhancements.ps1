param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$SkipTests)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$cmake=(Get-Command cmake -ErrorAction SilentlyContinue).Source
if(-not $cmake){$cmake=Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'}
$build=Join-Path $repo 'native\build\enhancements'
& $cmake -S (Join-Path $repo 'native\third_party\vectorxr') -B $build -G 'Visual Studio 17 2022' -A x64 -DDEPTHXR_BUILD_TESTS=ON -DDEPTHXR_BUILD_HARDWARE_PROBES=OFF -DDEPTHXR_FRAMEGEN_SPIRV=OFF
if($LASTEXITCODE){throw 'Enhancements configure failed.'}
$targets=@('XR_APILAYER_OPENGRIDXR_ENHANCEMENTS','depthxr_layer_load_tests','depthxr_osd_tests','depthxr_runtime_relay_tests','depthxr_framegen_presenter_tests','depthxr_framegen_tests','depthxr_d3d11_bridge_tests','opengrid-enhancements-policy-tests')
& $cmake --build $build --config $Configuration --target @targets --parallel 4
if($LASTEXITCODE){throw 'Enhancements build failed.'}
if(-not $SkipTests){
 $ctest=Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
 & $ctest --test-dir $build -C $Configuration --output-on-failure -R '^(opengrid-enhancements-policy|depthxr_layer_load_tests|depthxr_layer_crt_imports_tests|depthxr_osd_tests|depthxr_runtime_relay_tests|depthxr_framegen_submission_counter_tests|depthxr_framegen_d3d11_tests|depthxr_framegen_d3d12_tests|depthxr_d3d11_bridge_tests)$'
 if($LASTEXITCODE){throw 'Enhancements tests failed.'}
}
