param([string]$LayerOutput=(Join-Path $PSScriptRoot 'build/lmu_quadview.dll'),[switch]$StereoShaderOnly,[switch]$StereoProofOnly,[switch]$UploadBatchOnly,[switch]$FocusLeaseOnly,[switch]$AutoloadOnly,[switch]$AsyncLogOnly,[switch]$NvStereoShaderOnly,[switch]$NvMultiviewOnly,[switch]$EndFrameOnly,[switch]$AppImageOnly)
$ErrorActionPreference='Stop'
if(!(Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Run build-msvc.cmd to initialize the x64 compiler environment.'}
$LayerOutput=[IO.Path]::GetFullPath($LayerOutput)
$build=Join-Path $PSScriptRoot 'build/msvc'
New-Item -ItemType Directory -Path $build,([IO.Path]::GetDirectoryName($LayerOutput)) -Force|Out-Null
$common=@('/nologo','/std:c++17','/O2','/EHsc','/MT','/Zc:__cplusplus','/D_CRT_SECURE_NO_WARNINGS','/I../prototype/third_party','/W4','/wd4191','user32.lib','dxguid.lib',('/Fo'+$build+'\'))
function Compile([string[]]$Arguments){
    # A response file avoids Windows PowerShell 5 native argument quoting issues
    # for concatenated /Fo and /Fe paths in checkouts whose names contain spaces.
    $nativeArguments=@($Arguments|ForEach-Object{
        if($_ -match '^(/F[oe])(.+\s.+)$'){$Matches[1]+'"'+$Matches[2].Replace('\','/')+'"'}
        elseif($_ -match '\s'){'"'+$_+'"'}else{$_}
    })
    [IO.File]::WriteAllLines((Join-Path $build 'compile.rsp'),$nativeArguments,[Text.Encoding]::Unicode)
    & cl.exe '@build/msvc/compile.rsp'
    if($LASTEXITCODE -ne 0){throw 'C++ compilation failed'}
}
function Run-Test([string]$Name,[string[]]$Arguments=@()){
    & (Join-Path $build ($Name+'.exe')) @Arguments
    if($LASTEXITCODE -ne 0){throw "$Name failed: $Arguments"}
}
Push-Location $PSScriptRoot
try {
    if($AppImageOnly){
        Compile ($common+@('tests/app_image_lock_test.cpp','async_log.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'app_image_lock_test.exe'))))
        Run-Test 'app_image_lock_test'
        return
    }
    if($EndFrameOnly){
        Compile ($common+@('tests/end_frame_lock_test.cpp','async_log.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'end_frame_lock_test.exe'))))
        Run-Test 'end_frame_lock_test'
        foreach($test in @('frame_submission','benchmark_frame','benchmark_timing_ownership','outer_runtime_lock','late_begin_frame')){
            Compile ($common+@("tests/${test}_test.cpp",'async_log.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build ($test+'_test.exe')))))
            Run-Test ($test+'_test')
        }
        return
    }
    if($AsyncLogOnly){
        Compile ($common+@('tests/async_log_test.cpp','async_log.cpp',('/Fe'+(Join-Path $build 'async_log_test.exe'))))
        Compile ($common+@('/LD','tests/async_log_lifetime_fixture.cpp','async_log.cpp',('/Fe'+(Join-Path $build 'async_log_lifetime_fixture.dll'))))
        Run-Test 'async_log_test' @('--dll-lifetime')
        return
    }
    if($NvStereoShaderOnly){
        Compile ($common+@('tests/nv_stereo_shader_test.cpp','nv_stereo_shader.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'nv_stereo_shader_test.exe'))))
        Run-Test 'nv_stereo_shader_test'
        return
    }
    if($NvMultiviewOnly){
        Compile ($common+@('tests/nv_multiview_backend_test.cpp','nv_multiview_backend.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'nv_multiview_backend_test.exe'))))
        Run-Test 'nv_multiview_backend_test'
        return
    }
    . (Join-Path $PSScriptRoot '../tools/version.ps1')
    $versionFiles = Write-LmuVersionFiles (Join-Path $build 'native-version')
    $versionResource = Join-Path $build 'version.res'
    & rc.exe /nologo "/fo$versionResource" $versionFiles.ResourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Native version resource compilation failed' }
    Compile ($common+@('tests/autoload_policy_test.cpp',('/Fe'+(Join-Path $build 'autoload_policy_test.exe'))))
    Run-Test 'autoload_policy_test'
    Compile ($common+@('tests/autoload_activation_test.cpp',('/Fe'+(Join-Path $build 'autoload_activation_test.exe'))))
    Run-Test 'autoload_activation_test'
    Compile ($common+@('/LD','autoload.cpp',$versionResource,'advapi32.lib','ole32.lib','bcrypt.lib',('/Fe'+(Join-Path ([IO.Path]::GetDirectoryName($LayerOutput)) 'lmu_autostart.dll'))))
    Compile ($common+@('tests/autoload_passthrough_test.cpp','autoload.cpp','advapi32.lib','ole32.lib','bcrypt.lib',('/Fe'+(Join-Path $build 'autoload_passthrough_test.exe'))))
    Run-Test 'autoload_passthrough_test'
    Compile ($common+@('tests/autoload_verification_test.cpp','advapi32.lib','ole32.lib','bcrypt.lib',('/Fe'+(Join-Path $build 'autoload_verification_test.exe'))))
    Run-Test 'autoload_verification_test'
    if($AutoloadOnly){return}
    if($FocusLeaseOnly){
        Compile ($common+@('tests/focus_image_lease_test.cpp','async_log.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'focus_image_lease_test.exe'))))
        Run-Test 'focus_image_lease_test'
        return
    }
    if($UploadBatchOnly){
        Compile ($common+@('tests/native_upload_batch_test.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'native_upload_batch_test.exe'))))
        Run-Test 'native_upload_batch_test'
        return
    }
    if($StereoProofOnly){
        Compile ($common+@('tests/stereo_draw_proof_test.cpp','stereo_shader.cpp','stereo_draw_proof.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'stereo_draw_proof_test.exe'))))
        Run-Test 'stereo_draw_proof_test'
        return
    }
    if($StereoShaderOnly){
        Compile ($common+@('tests/stereo_shader_test.cpp','stereo_shader.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'stereo_shader_test.exe'))))
        Run-Test 'stereo_shader_test'
        return
    }
    foreach($part in @('buffer','hook','trampoline')){
        Compile @('/nologo','/TC','/O2','/MT','/c',"vendor/minhook-1.3.4/src/$part.c",('/Fo'+(Join-Path $build ('mh_'+$part+'.obj'))))
    }
    Compile @('/nologo','/TC','/O2','/MT','/c','vendor/minhook-1.3.4/src/hde/hde64.c',('/Fo'+(Join-Path $build 'mh_hde64.obj')))
    $hooks=@('buffer','hook','trampoline','hde64')|ForEach-Object{Join-Path $build ('mh_'+$_+'.obj')}
    $basic=@('math','focus_box','native_xr_views','gpu_timing','render_profile','swapchain_sampling','pipeline_stats','benchmark_control','vrs_scope_policy','nv_vrs','ui_d3d_state','focus_overlay','outer_resample','native_outer_texture','native_outer_bindings','stereo_instancing','stereo_audit_abi','stereo_audit','stereo_shader','stereo_draw_proof','native_upload_reuse','native_upload_batch','focus_image_lease')
    $basic += @('benchmark_timing_ownership','render_view_snapshot','frame_submission','late_begin_frame','view_crop','benchmark_native','benchmark_overlay','benchmark_frame','benchmark_gpu','vrs_foveation','vrs_gaze','native_outer_bank')
    $basic += @('input_validation','async_log','native_pair_pass','nv_stereo_shader','nv_multiview_backend','xr_active_state','diagnostic_sampling','upload_reuse_toggle','outer_runtime_lock','end_frame_lock','locate_runtime_lock','app_image_lock','cpu_cache_affinity')
    foreach($test in $basic){
        $sources=@("tests/${test}_test.cpp")
        if($test -eq 'nv_vrs'){$sources+='nv_vrs.cpp'}
        if($test -eq 'stereo_shader'){$sources+='stereo_shader.cpp'}
        if($test -eq 'stereo_draw_proof'){$sources+=@('stereo_shader.cpp','stereo_draw_proof.cpp')}
        if($test -in @('input_validation','focus_image_lease','frame_submission','render_view_snapshot','late_begin_frame','benchmark_frame','benchmark_timing_ownership','xr_active_state','outer_runtime_lock','end_frame_lock','locate_runtime_lock','app_image_lock')){$sources+='async_log.cpp'}
        if($test -in @('async_log','nv_stereo_shader','nv_multiview_backend')){$sources+="${test}.cpp"}
        if($test -eq 'stereo_audit'){$sources+=@('stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp');$sources+=$hooks}
        Compile ($common+$sources+@('d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+(Join-Path $build ($test+'_test.exe')))))
        if($test -eq 'async_log'){
            Compile ($common+@('/LD','tests/async_log_lifetime_fixture.cpp','async_log.cpp',('/Fe'+(Join-Path $build 'async_log_lifetime_fixture.dll'))))
            Run-Test 'async_log_test' @('--dll-lifetime')
        }
        elseif($test -eq 'vrs_scope_policy'){Run-Test ($test+'_test') @('--policy-only')}
        else{Run-Test ($test+'_test')}
    }
    foreach($test in @('engine_hooks','native_outer_fallback','benchmark_engine')){
        Compile ($common+@("tests/${test}_test.cpp",'nv_vrs.cpp','stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp')+$hooks+@('d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+(Join-Path $build ($test+'_test.exe')))))
        Run-Test ($test+'_test')
        if($test -eq 'engine_hooks'){Run-Test 'engine_hooks_test' @('--runtime-upload-toggle')}
    }
    Run-Test 'engine_hooks_test' @('--process-identity')
    $identityFixture=Join-Path $build 'identity-fixture'
    New-Item -ItemType Directory -Path $identityFixture -Force | Out-Null
    $identityExecutable=Join-Path $identityFixture 'Le Mans Ultimate.exe'
    Copy-Item -LiteralPath (Join-Path $build 'engine_hooks_test.exe') -Destination $identityExecutable -Force
    & $identityExecutable '--process-identity'
    if($LASTEXITCODE -ne 0){throw 'Native process identity fixture failed'}
    Compile ($common+@('xr_layer.cpp','xr_layer_tests.cpp','async_log.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'xr_layer_tests.exe'))))
    $cases=@(@(),@('--srgb'),@('--stats'),@('--srgb','--stats'),@('--scene-stats'),@('--srgb','--scene-stats'),@('--capture'),@('--srgb','--capture'),@('--capture-no-trigger'),@('--capture-invalid'),@('--debug-focus'),@('--srgb','--debug-focus'),@('--scaled'),@('--srgb','--scaled'),@('--scaled','--debug-focus'),@('--srgb','--scaled','--debug-focus'),@('--scaled','--capture'),@('--invalid-sizes'),@('--focus-box'),@('--focus-box-large'),@('--invalid-focus-box'),@('--focus-box','--debug-focus'),@('--focus-box-large','--debug-focus'),@('--focus-box','--scaled','--srgb','--debug-focus'),@('--focus-asymmetric'),@('--focus-asymmetric','--debug-focus'),@('--focus-asymmetric','--scaled','--srgb','--debug-focus'),@('--runtime-settings'),@('--runtime-settings','--srgb'),@('--runtime-settings','--debug-focus'),@('--runtime-settings','--srgb','--debug-focus'),@('--runtime-equal'),@('--runtime-equal','--debug-focus'),@('--runtime-axis-equal'),@('--runtime-axis-equal','--debug-focus'),@('--runtime-too-wide'),@('--runtime-too-tall'),@('--runtime-overlay'),@('--runtime-overlay','--srgb'),@('--runtime-overlay','--debug-focus'),@('--runtime-overlay','--srgb','--debug-focus'),@('--runtime-settings','--fixed-foveation'),@('--runtime-settings','--srgb','--fixed-foveation'),@('--runtime-overlay','--fixed-foveation'),@('--runtime-overlay','--srgb','--fixed-foveation'),@('--runtime-settings','--no-eye-tracking'),@('--runtime-overlay','--srgb','--no-eye-tracking'),@('--runtime-equal','--outer-blur'),@('--runtime-equal','--outer-blur','--srgb'),@('--runtime-equal','--outer-blur','--typed-srgb-runtime'))
    $cases+=@(@('--focus-offset'),@('--focus-offset','--focus-asymmetric'),@('--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset'),@('--runtime-settings','--focus-offset','--fixed-foveation'),@('--runtime-settings','--focus-offset','--no-eye-tracking'),@('--runtime-settings','--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset','--srgb','--debug-focus'),@('--invalid-focus-offset'),@('--runtime-settings','--invalid-focus-offset'))
    $cases+=@(@('--crop'),@('--crop','--scaled'),@('--crop','--runtime-settings'),@('--crop','--runtime-settings','--srgb'),@('--crop','--runtime-settings','--focus-offset'),@('--invalid-crop'),@('--invalid-crop','--runtime-settings'))
    $cases+=@(@('--runtime-settings','--legacy-dynamic-env'),@('--runtime-settings','--legacy-dynamic-env','--focus-offset'),@('--runtime-settings','--legacy-dynamic-env','--crop'),@('--runtime-settings','--legacy-dynamic-env','--debug-focus'),@('--runtime-settings','--legacy-dynamic-env','--diagnostics-off'))
    foreach($case in $cases){Run-Test 'xr_layer_tests' $case}
    foreach($case in @(@('--runtime-settings','--sampled-inputs'),@('--runtime-settings','--sampled-inputs','--srgb'),@('--runtime-settings','--sampled-inputs','--debug-focus'),@('--runtime-overlay','--sampled-inputs','--srgb'),@('--runtime-settings','--reject-sampled-inputs'),@('--runtime-settings','--reject-sampled-inputs','--srgb'))){Run-Test 'xr_layer_tests' $case}
    foreach($case in @(@('--runtime-settings','--profile-on-close'),@('--runtime-settings','--profile-close-other-thread','--sampled-inputs'))){Run-Test 'xr_layer_tests' $case}
    Run-Test 'xr_layer_tests' @('--runtime-settings','--profile-menu-frames')
    Run-Test 'xr_layer_tests' @('--runtime-settings','--diagnostics-off')
    Run-Test 'render_profile_test' @('--diagnostics-off')
    Run-Test 'frame_submission_test' @('--profile')
    Run-Test 'benchmark_frame_test' @('--diagnostics-off')
    Run-Test 'stereo_audit_test' @('--diagnostics-off')
    Run-Test 'stereo_audit_test' @('--shader-export')
    Run-Test 'stereo_audit_test' @('--pair-proof')
    $vrsResourceSource=Join-Path $build 'native-version/vrs-check.rc'
    (Get-Content -LiteralPath $versionFiles.ResourcePath -Raw).Replace('FILETYPE VFT_DLL','FILETYPE VFT_APP').Replace('LMU Quad View OpenXR layer','LMU Quad View VRS hardware check').Replace('lmu_quadview.dll','VrsHardwareCheck.exe') | Set-Content -LiteralPath $vrsResourceSource -Encoding ASCII
    $vrsResource=Join-Path $build 'vrs-check.res'
    & rc.exe /nologo "/fo$vrsResource" $vrsResourceSource
    if($LASTEXITCODE -ne 0){throw 'VRS diagnostic version resource compilation failed'}
    Compile ($common+@('tests/nv_vrs_hardware_test.cpp','nv_vrs.cpp',$vrsResource,'d3d11.lib','d3dcompiler.lib','dxgi.lib',('/Fe'+(Join-Path $build 'nv_vrs_hardware_test.exe'))))
    Compile ($common+@('/LD','xr_layer.cpp','async_log.cpp','engine_bridge.cpp','nv_vrs.cpp','stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp')+$hooks+@($versionResource,'d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+$LayerOutput)))
    $hasher=[Security.Cryptography.SHA256]::Create()
    $binaryStream=[IO.File]::OpenRead($LayerOutput)
    try { Write-Output ('SHA256 '+[BitConverter]::ToString($hasher.ComputeHash($binaryStream)).Replace('-','')) }
    finally { $binaryStream.Dispose();$hasher.Dispose() }
} finally {Pop-Location}
