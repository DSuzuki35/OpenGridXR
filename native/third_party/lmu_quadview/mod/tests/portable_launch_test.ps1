$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../portable-launch.ps1')
function Assert($value,$message) { if (!$value) { throw $message } }
$root=Join-Path ([IO.Path]::GetTempPath()) ('qv-launch-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    # Exercise executable validation only; never launch a process from this test.
    $source=Get-Content -LiteralPath (Join-Path $PSScriptRoot '../launch.ps1') -Raw
    $start=$source.IndexOf('$gameExecutable =')
    $end=$source.IndexOf('if (Get-Process', $start)
    $validate=[scriptblock]::Create($source.Substring($start,$end-$start)+'; $gameHash')
    $GameDirectory=$root
    $game=Join-Path $root 'Le Mans Ultimate.exe'
    [IO.File]::WriteAllBytes($game,[byte[]](1,2,3))
    Assert ((& $validate) -eq '039058C6F2C0CB492C533B0A4D14EF77CC0F78ABCCCED5287D84A1A2011CFB81') 'Alternate game hash rejected or incorrect metadata hash'
    [IO.File]::Delete($game)
    $missing=$false;try{& $validate | Out-Null}catch{$missing=$true}
    Assert $missing 'Missing game executable accepted'
    $provider=Join-Path $root "provider's files"
    New-Item -ItemType Directory -Path $provider | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $provider 'layer.dll'),[byte[]](1,2,3))
    $manifest=Join-Path $provider 'quad.json'
    @{file_format_version='1.0.0';api_layer=@{name='XR_APILAYER_MBUCCHIA_quad_views_foveated';library_path='./layer.dll';api_version='1.0';implementation_version='1';disable_environment='DISABLE_QUAD_VIEWS_FOVEATED';enable_environment='ENABLE_TEST'}} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifest
    $before=Get-Content -LiteralPath $manifest -Raw
    $plan=New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'run')
    Assert ($plan.Layers -eq 'XR_APILAYER_LMU_quadview;XR_APILAYER_MBUCCHIA_quad_views_foveated') 'Wrong Windows layer order/delimiter'
    Assert ($plan.DisableVariable -eq 'DISABLE_QUAD_VIEWS_FOVEATED') 'Missing process-local implicit disable'
    Assert ($plan.DisableVariables -notcontains 'DISABLE_XR_APILAYER_DIENERTECH_VECTORXR') 'QVFR must allow the separately configured VectorXR overlay'
    $copy=Get-Content -LiteralPath $plan.ExplicitManifest -Raw | ConvertFrom-Json
    Assert ([IO.Path]::IsPathRooted($copy.api_layer.library_path)) 'Library must remain at original location'
    Assert (!$copy.api_layer.PSObject.Properties['disable_environment'] -and !$copy.api_layer.PSObject.Properties['enable_environment']) 'Explicit copy retained implicit activation switches'
    Assert ((Get-Content -LiteralPath $manifest -Raw) -ceq $before) 'Original manifest modified'
    Assert ($null -eq [Environment]::GetEnvironmentVariable('DISABLE_QUAD_VIEWS_FOVEATED','Process')) 'Host environment changed'
    $native=New-LmuLayerPlan -Backend Native -ManifestRoot $root
    Assert ($native.Layers -eq 'XR_APILAYER_LMU_quadview') 'Native backend changed'
    $vectorManifest=Join-Path $provider 'vector.json'
    $vectorBefore=$before.Replace('XR_APILAYER_MBUCCHIA_quad_views_foveated','XR_APILAYER_DIENERTECH_VECTORXR').Replace('DISABLE_QUAD_VIEWS_FOVEATED','DISABLE_XR_APILAYER_DIENERTECH_VECTORXR')
    [IO.File]::WriteAllText($vectorManifest,$vectorBefore)
    $customVector=Join-Path $provider 'custom-vector.json'
    [IO.File]::WriteAllText($customVector,$vectorBefore.Replace('DISABLE_XR_APILAYER_DIENERTECH_VECTORXR','DISABLE_VENDOR_VECTORXR'))
    $overlayDisables=@(Get-LmuImplicitDisables -Backend QVFR -AdditionalManifests @($vectorManifest,$customVector))
    Assert ($overlayDisables -notcontains 'DISABLE_XR_APILAYER_DIENERTECH_VECTORXR' -and $overlayDisables -notcontains 'DISABLE_VENDOR_VECTORXR') 'Registered VectorXR manifest reintroduced overlay block'
    Assert ($native.DisableVariables -contains 'DISABLE_XR_APILAYER_DIENERTECH_VECTORXR') 'Native backend isolation changed'
    $vector=New-LmuLayerPlan -Backend VectorXR -ManifestRoot $root -QuadViewsManifest $vectorManifest -RunDirectory (Join-Path $root 'vector-run')
    Assert ($vector.Layers -eq 'XR_APILAYER_LMU_quadview;XR_APILAYER_DIENERTECH_VECTORXR') 'VectorXR order/name'
    Assert ($vector.DisableVariables -contains 'DISABLE_QUAD_VIEWS_FOVEATED') 'Competing QVFR not disabled'
    Assert ($vector.DisableVariables -contains 'DISABLE_XR_APILAYER_DIENERTECH_VECTORXR') 'Duplicate implicit VectorXR not disabled'
    Assert ($vector.DisableVariables -contains 'DISABLE_XR_APILAYER_MBUCCHIA_toolkit') 'Toolkit not bypassed for VectorXR'
    Assert (([IO.File]::ReadAllText($vectorManifest)) -ceq $vectorBefore) 'Installed VectorXR manifest modified'
    $vectorCopy=Get-Content -LiteralPath $vector.ExplicitManifest -Raw|ConvertFrom-Json
    Assert (!$vectorCopy.api_layer.PSObject.Properties['disable_environment'] -and !$vectorCopy.api_layer.PSObject.Properties['enable_environment']) 'VectorXR explicit copy kept activation switches'
    $failed=$false;try{New-LmuLayerPlan -Backend VectorXR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'wrong-vector')|Out-Null}catch{$failed=$true}
    Assert $failed 'VectorXR accepted mbucchia manifest'
    $bad=$before.Replace('layer.dll','missing.dll'); Set-Content -LiteralPath $manifest -Value $bad
    $failed=$false;try { New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'bad') | Out-Null } catch { $failed=$true }
    Assert $failed 'Missing provider DLL accepted'
    $bad=$before.Replace('XR_APILAYER_MBUCCHIA_quad_views_foveated','unrelated_layer');Set-Content -LiteralPath $manifest -Value $bad
    $failed=$false;try { New-LmuLayerPlan -Backend QVFR -ManifestRoot $root -QuadViewsManifest $manifest -RunDirectory (Join-Path $root 'bad-name') | Out-Null } catch { $failed=$true }
    Assert $failed 'Wrong provider accepted'
    Write-Output 'Portable launch tests passed: QVFR/VectorXR/native, layer order, manifest isolation, original DLL path, duplicate/competing provider switches, wrong provider rejection'
} finally {
    if ([IO.Path]::GetFullPath($root).StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $root -Recurse -Force }
}
