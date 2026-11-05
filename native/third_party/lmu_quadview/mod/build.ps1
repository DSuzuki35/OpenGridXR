param(
    [string]$LayerOutput = (Join-Path $PSScriptRoot 'build/lmu_quadview.dll'),
    [string]$AnchorImage
)
$ErrorActionPreference = 'Stop'
$LayerOutput = [IO.Path]::GetFullPath($LayerOutput)
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    . (Join-Path $PSScriptRoot '../tools/version.ps1')
    $versionFiles = Write-LmuVersionFiles (Join-Path $PSScriptRoot 'build/native-version')
    & windres $versionFiles.ResourcePath -O coff -o build/version.o
    if ($LASTEXITCODE -ne 0) { throw 'Native version resource compilation failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/math_test.cpp -o build/math_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Math test compilation failed' }
    & .\build\math_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Math tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/focus_box_test.cpp -o build/focus_box_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus box tests compilation failed' }
    & .\build\focus_box_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus box tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/view_crop_test.cpp -o build/view_crop_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'View crop tests compilation failed' }
    & .\build\view_crop_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'View crop tests failed' }
    foreach ($test in @('dynamic_focus','dynamic_focus_frame','benchmark_native','benchmark_overlay','benchmark_frame','benchmark_gpu')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party "tests/${test}_test.cpp" -ld3d11 -ld3dcompiler -ldxguid -luser32 -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test failed" }
    }
    & .\build\benchmark_frame_test.exe --diagnostics-off
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark diagnostics-off test failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/native_xr_views_test.cpp -o build/native_xr_views_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native XR view tests compilation failed' }
    & .\build\native_xr_views_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native XR view tests failed' }
    foreach ($part in @('buffer','hook','trampoline')) {
        & gcc -O2 -c "vendor/minhook-1.3.4/src/$part.c" -o "build/mh_$part.o"
        if ($LASTEXITCODE -ne 0) { throw "MinHook $part compilation failed" }
    }
    & gcc -O2 -c vendor/minhook-1.3.4/src/hde/hde64.c -o build/mh_hde64.o
    if ($LASTEXITCODE -ne 0) { throw 'MinHook decoder compilation failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/engine_hooks_test.cpp nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o build/engine_hooks_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Engine hook tests compilation failed' }
    & .\build\engine_hooks_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Engine hook tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/native_outer_fallback_test.cpp nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o build/native_outer_fallback_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native fallback tests compilation failed' }
    & .\build\native_outer_fallback_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native fallback tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/gpu_timing_test.cpp -ld3d11 -o build/gpu_timing_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPU timing tests compilation failed' }
    & .\build\gpu_timing_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPU timing tests failed' }
    foreach ($test in @('pipeline_stats','benchmark_control','render_profile','swapchain_sampling','stereo_audit_abi')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party "tests/${test}_test.cpp" -ld3d11 -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests failed" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/stereo_audit_test.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o build/stereo_audit_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo audit tests compilation failed' }
    & .\build\stereo_audit_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo audit tests failed' }
    & .\build\stereo_audit_test.exe --diagnostics-off
    if ($LASTEXITCODE -ne 0) { throw 'Stereo audit master-off tests failed' }
    & .\build\stereo_audit_test.exe --shader-export
    if ($LASTEXITCODE -ne 0) { throw 'Stereo audit shader export tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/stereo_shader_test.cpp stereo_shader.cpp -ld3d11 -ld3dcompiler -ldxguid -luser32 -o build/stereo_shader_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo shader conversion compilation failed' }
    & .\build\stereo_shader_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo shader conversion tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/stereo_draw_proof_test.cpp stereo_shader.cpp stereo_draw_proof.cpp -ld3d11 -ld3dcompiler -ldxguid -luser32 -o build/stereo_draw_proof_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo private proof compilation failed' }
    & .\build\stereo_draw_proof_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Stereo private proof tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/native_upload_reuse_test.cpp -ld3d11 -o build/native_upload_reuse_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native upload reuse compilation failed' }
    & .\build\native_upload_reuse_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native upload reuse tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/native_upload_batch_test.cpp -ld3d11 -ld3dcompiler -ldxguid -o build/native_upload_batch_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native upload batch compilation failed' }
    & .\build\native_upload_batch_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native upload batch tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/focus_image_lease_test.cpp -ld3d11 -ld3dcompiler -ldxguid -luser32 -o build/focus_image_lease_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus image ownership compilation failed' }
    & .\build\focus_image_lease_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Focus image ownership tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/frame_submission_test.cpp -ld3d11 -ld3dcompiler -ldxguid -luser32 -o build/frame_submission_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Frame submission tests compilation failed' }
    & .\build\frame_submission_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Frame submission tests failed' }
    & .\build\frame_submission_test.exe --profile
    if ($LASTEXITCODE -ne 0) { throw 'Frame submission profiling tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/benchmark_engine_test.cpp nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o build/benchmark_engine_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark engine tests compilation failed' }
    & .\build\benchmark_engine_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Benchmark engine tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/vrs_scope_policy_test.cpp -o build/vrs_scope_policy_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'VRS scope policy compilation failed' }
    if ($AnchorImage) { & .\build\vrs_scope_policy_test.exe $AnchorImage }
    else { & .\build\vrs_scope_policy_test.exe --policy-only }
    if ($LASTEXITCODE -ne 0) { throw 'VRS scope policy tests failed' }
    foreach ($test in @('vrs_foveation','vrs_gaze')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -I ../prototype/third_party "tests/${test}_test.cpp" -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test failed" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party tests/nv_vrs_test.cpp nv_vrs.cpp -ld3d11 -o build/nv_vrs_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NV VRS helper tests compilation failed' }
    & .\build\nv_vrs_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NV VRS helper tests failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static tests/ui_d3d_state_test.cpp -ld3d11 -ld3dcompiler -ldxguid -o build/ui_d3d_state_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'D3D11 state tests compilation failed' }
    & .\build\ui_d3d_state_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'D3D11 state tests failed' }
    foreach ($test in @('focus_overlay','outer_resample','native_outer_texture','native_outer_bindings','stereo_instancing','native_outer_bank')) {
        & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static "tests/${test}_test.cpp" -ld3d11 -ld3dcompiler -ldxguid -o "build/${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests compilation failed" }
        & ".\build\${test}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "$test tests failed" }
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -I ../prototype/third_party xr_layer.cpp xr_layer_tests.cpp -ld3d11 -ld3dcompiler -ldxguid -o build/xr_layer_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'XR tests compilation failed' }
    foreach ($testArgs in @(@('--focus-offset'),@('--focus-offset','--focus-asymmetric'),@('--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset'),@('--runtime-settings','--focus-offset','--fixed-foveation'),@('--runtime-settings','--focus-offset','--no-eye-tracking'),@('--runtime-settings','--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset','--srgb','--debug-focus'),@('--invalid-focus-offset'),@('--runtime-settings','--invalid-focus-offset'))) {
        & .\build\xr_layer_tests.exe @testArgs
        if ($LASTEXITCODE -ne 0) { throw "Focus offset XR tests failed: $testArgs" }
    }
    foreach ($testArgs in @(@(),@('--srgb'),@('--stats'),@('--srgb','--stats'),@('--scene-stats'),@('--srgb','--scene-stats'),@('--capture'),@('--srgb','--capture'),@('--capture-no-trigger'),@('--capture-invalid'),@('--debug-focus'),@('--srgb','--debug-focus'),@('--scaled'),@('--srgb','--scaled'),@('--scaled','--debug-focus'),@('--srgb','--scaled','--debug-focus'),@('--scaled','--capture'),@('--invalid-sizes'),@('--focus-box'),@('--focus-box-large'),@('--invalid-focus-box'),@('--focus-box','--debug-focus'),@('--focus-box-large','--debug-focus'),@('--focus-box','--scaled','--srgb','--debug-focus'),@('--focus-asymmetric'),@('--focus-asymmetric','--debug-focus'),@('--focus-asymmetric','--scaled','--srgb','--debug-focus'),@('--runtime-settings'),@('--runtime-settings','--srgb'),@('--runtime-settings','--debug-focus'),@('--runtime-settings','--srgb','--debug-focus'),@('--runtime-equal'),@('--runtime-equal','--debug-focus'),@('--runtime-axis-equal'),@('--runtime-axis-equal','--debug-focus'),@('--runtime-too-wide'),@('--runtime-too-tall'),@('--runtime-overlay'),@('--runtime-overlay','--srgb'),@('--runtime-overlay','--debug-focus'),@('--runtime-overlay','--srgb','--debug-focus'))) {
        & .\build\xr_layer_tests.exe @testArgs
        if ($LASTEXITCODE -ne 0) { throw "XR tests failed: $testArgs" }
    }
    foreach ($testArgs in @(@('--runtime-settings','--fixed-foveation'),@('--runtime-settings','--srgb','--fixed-foveation'),@('--runtime-overlay','--fixed-foveation'),@('--runtime-overlay','--srgb','--fixed-foveation'),@('--runtime-settings','--no-eye-tracking'),@('--runtime-overlay','--srgb','--no-eye-tracking'))) {
        & .\build\xr_layer_tests.exe @testArgs
        if ($LASTEXITCODE -ne 0) { throw "Fixed foveation tests failed: $testArgs" }
    }
    foreach ($testArgs in @(@('--runtime-equal','--outer-blur'),@('--runtime-equal','--outer-blur','--srgb'),@('--runtime-equal','--outer-blur','--typed-srgb-runtime'))) {
        & .\build\xr_layer_tests.exe @testArgs
        if ($LASTEXITCODE -ne 0) { throw "Outer blur tests failed: $testArgs" }
    }
    foreach($testArgs in @(@('--runtime-settings','--sampled-inputs'),@('--runtime-settings','--sampled-inputs','--srgb'),@('--runtime-settings','--sampled-inputs','--debug-focus'),@('--runtime-overlay','--sampled-inputs','--srgb'),@('--runtime-settings','--reject-sampled-inputs'),@('--runtime-settings','--reject-sampled-inputs','--srgb'))){
        & .\build\xr_layer_tests.exe @testArgs
        if($LASTEXITCODE -ne 0){throw 'Sampled-input XR tests failed'}
    }
    foreach($testArgs in @(@('--runtime-settings','--profile-on-close'),@('--runtime-settings','--profile-close-other-thread','--sampled-inputs'))){
        & .\build\xr_layer_tests.exe @testArgs
        if($LASTEXITCODE -ne 0){throw 'Capture save-on-close XR tests failed'}
    }
    & .\build\xr_layer_tests.exe --runtime-settings --profile-menu-frames
    if ($LASTEXITCODE -ne 0) { throw 'XR menu/scene profiling boundary tests failed' }
    & .\build\xr_layer_tests.exe --runtime-settings --diagnostics-off
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostics-off XR tests failed' }
    & .\build\render_profile_test.exe --diagnostics-off
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostics-off profiler tests failed' }
    foreach($testArgs in @(@('--crop'),@('--crop','--scaled'),@('--crop','--runtime-settings'),@('--crop','--runtime-settings','--srgb'),@('--crop','--runtime-settings','--focus-offset'),@('--invalid-crop'),@('--invalid-crop','--runtime-settings'))){
        & .\build\xr_layer_tests.exe @testArgs
        if($LASTEXITCODE -ne 0){throw 'Rendered crop XR tests failed'}
    }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Werror -static -shared -I ../prototype/third_party xr_layer.cpp engine_bridge.cpp nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp build/mh_buffer.o build/mh_hook.o build/mh_trampoline.o build/mh_hde64.o build/version.o -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o $LayerOutput
    if ($LASTEXITCODE -ne 0) { throw 'Layer compilation failed' }
    Get-FileHash -LiteralPath $LayerOutput -Algorithm SHA256 | Select-Object Hash
} finally { Pop-Location }
