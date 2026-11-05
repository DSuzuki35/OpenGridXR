param([string]$LayerOutput=(Join-Path $PSScriptRoot 'build/lmu_quadview.dll'),[switch]$StereoShaderOnly,[switch]$StereoProofOnly,[switch]$UploadBatchOnly,[switch]$FocusLeaseOnly)
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
    . (Join-Path $PSScriptRoot '../tools/version.ps1')
    $versionFiles = Write-LmuVersionFiles (Join-Path $build 'native-version')
    $versionResource = Join-Path $build 'version.res'
    & rc.exe /nologo "/fo$versionResource" $versionFiles.ResourcePath
    if ($LASTEXITCODE -ne 0) { throw 'Native version resource compilation failed' }
    if($FocusLeaseOnly){
        Compile ($common+@('tests/focus_image_lease_test.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'focus_image_lease_test.exe'))))
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
    $basic += @('dynamic_focus','dynamic_focus_frame','render_view_snapshot','frame_submission','view_crop','benchmark_native','benchmark_overlay','benchmark_frame','benchmark_gpu','vrs_foveation','vrs_gaze','native_outer_bank')
    foreach($test in $basic){
        $sources=@("tests/${test}_test.cpp")
        if($test -eq 'nv_vrs'){$sources+='nv_vrs.cpp'}
        if($test -eq 'stereo_shader'){$sources+='stereo_shader.cpp'}
        if($test -eq 'stereo_draw_proof'){$sources+=@('stereo_shader.cpp','stereo_draw_proof.cpp')}
        if($test -eq 'stereo_audit'){$sources+=@('stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp');$sources+=$hooks}
        Compile ($common+$sources+@('d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+(Join-Path $build ($test+'_test.exe')))))
        if($test -eq 'vrs_scope_policy'){Run-Test ($test+'_test') @('--policy-only')}
        else{Run-Test ($test+'_test')}
    }
    foreach($test in @('engine_hooks','native_outer_fallback','benchmark_engine')){
        Compile ($common+@("tests/${test}_test.cpp",'nv_vrs.cpp','stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp')+$hooks+@('d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+(Join-Path $build ($test+'_test.exe')))))
        Run-Test ($test+'_test')
    }
    Compile ($common+@('xr_layer.cpp','xr_layer_tests.cpp','d3d11.lib','d3dcompiler.lib',('/Fe'+(Join-Path $build 'xr_layer_tests.exe'))))
    $cases=@(@(),@('--srgb'),@('--stats'),@('--srgb','--stats'),@('--scene-stats'),@('--srgb','--scene-stats'),@('--capture'),@('--srgb','--capture'),@('--capture-no-trigger'),@('--capture-invalid'),@('--debug-focus'),@('--srgb','--debug-focus'),@('--scaled'),@('--srgb','--scaled'),@('--scaled','--debug-focus'),@('--srgb','--scaled','--debug-focus'),@('--scaled','--capture'),@('--invalid-sizes'),@('--focus-box'),@('--focus-box-large'),@('--invalid-focus-box'),@('--focus-box','--debug-focus'),@('--focus-box-large','--debug-focus'),@('--focus-box','--scaled','--srgb','--debug-focus'),@('--focus-asymmetric'),@('--focus-asymmetric','--debug-focus'),@('--focus-asymmetric','--scaled','--srgb','--debug-focus'),@('--runtime-settings'),@('--runtime-settings','--srgb'),@('--runtime-settings','--debug-focus'),@('--runtime-settings','--srgb','--debug-focus'),@('--runtime-equal'),@('--runtime-equal','--debug-focus'),@('--runtime-axis-equal'),@('--runtime-axis-equal','--debug-focus'),@('--runtime-too-wide'),@('--runtime-too-tall'),@('--runtime-overlay'),@('--runtime-overlay','--srgb'),@('--runtime-overlay','--debug-focus'),@('--runtime-overlay','--srgb','--debug-focus'),@('--runtime-settings','--fixed-foveation'),@('--runtime-settings','--srgb','--fixed-foveation'),@('--runtime-overlay','--fixed-foveation'),@('--runtime-overlay','--srgb','--fixed-foveation'),@('--runtime-settings','--no-eye-tracking'),@('--runtime-overlay','--srgb','--no-eye-tracking'),@('--runtime-equal','--outer-blur'),@('--runtime-equal','--outer-blur','--srgb'),@('--runtime-equal','--outer-blur','--typed-srgb-runtime'))
    $cases+=@(@('--focus-offset'),@('--focus-offset','--focus-asymmetric'),@('--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset'),@('--runtime-settings','--focus-offset','--fixed-foveation'),@('--runtime-settings','--focus-offset','--no-eye-tracking'),@('--runtime-settings','--focus-offset','--debug-focus'),@('--runtime-settings','--focus-offset','--srgb','--debug-focus'),@('--invalid-focus-offset'),@('--runtime-settings','--invalid-focus-offset'))
    $cases+=@(@('--crop'),@('--crop','--scaled'),@('--crop','--runtime-settings'),@('--crop','--runtime-settings','--srgb'),@('--crop','--runtime-settings','--focus-offset'),@('--invalid-crop'),@('--invalid-crop','--runtime-settings'))
    $cases+=@(@('--runtime-settings','--dynamic-focus'),@('--runtime-settings','--dynamic-focus','--focus-offset'),@('--runtime-settings','--dynamic-focus','--crop'),@('--runtime-settings','--dynamic-focus','--debug-focus'),@('--runtime-settings','--dynamic-focus','--diagnostics-off'))
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
    Compile ($common+@('/LD','xr_layer.cpp','engine_bridge.cpp','nv_vrs.cpp','stereo_audit.cpp','stereo_shader.cpp','stereo_draw_proof.cpp')+$hooks+@($versionResource,'d3d11.lib','d3dcompiler.lib','advapi32.lib',('/Fe'+$LayerOutput)))
    $hasher=[Security.Cryptography.SHA256]::Create()
    $binaryStream=[IO.File]::OpenRead($LayerOutput)
    try { Write-Output ('SHA256 '+[BitConverter]::ToString($hasher.ComputeHash($binaryStream)).Replace('-','')) }
    finally { $binaryStream.Dispose();$hasher.Dispose() }
} finally {Pop-Location}
