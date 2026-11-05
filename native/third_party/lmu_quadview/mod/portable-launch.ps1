# Process-local layer selection. Never changes runtime registration or user settings.
function Find-LmuQuadViewsManifest {
    param([string]$Path,[ValidateSet('QVFR','VectorXR')][string]$Backend='QVFR')
    $expectedName=if($Backend -eq 'VectorXR'){'XR_APILAYER_DIENERTECH_VECTORXR'}else{'XR_APILAYER_MBUCCHIA_quad_views_foveated'}
    if ($Path) {
        if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Backend manifest not found: $Path" }
        return (Resolve-Path -LiteralPath $Path).Path
    }
    $candidates=[Collections.Generic.List[string]]::new()
    foreach ($hive in @('HKLM','HKCU')) {
        $key=Get-Item -LiteralPath "${hive}:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" -ErrorAction SilentlyContinue
        if ($key) { foreach ($name in $key.GetValueNames()) { if ($name.EndsWith('.json',[StringComparison]::OrdinalIgnoreCase)) { $candidates.Add($name) } } }
    }
    $directories=if($Backend -eq 'VectorXR'){@('VectorXR')}else{@('OpenXR-Quad-Views-Foveated','Quad-Views-Foveated')}
    foreach ($directory in $directories) {
        if ($env:ProgramFiles) {
            $folder=Join-Path $env:ProgramFiles $directory
            if (Test-Path -LiteralPath $folder -PathType Container) {
                foreach ($file in Get-ChildItem -LiteralPath $folder -Filter '*.json' -File -Recurse -ErrorAction SilentlyContinue) { $candidates.Add($file.FullName) }
            }
        }
    }
    if($Backend -eq 'VectorXR') {
        foreach($relative in @('Programs\VectorXR','VectorXR')) {
            $folder=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) $relative
            if(Test-Path -LiteralPath $folder -PathType Container) {
                foreach($file in Get-ChildItem -LiteralPath $folder -Filter '*.json' -File -Recurse -ErrorAction SilentlyContinue){$candidates.Add($file.FullName)}
            }
        }
    }
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (!(Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        try { $doc=Get-Content -LiteralPath $candidate -Raw | ConvertFrom-Json -ErrorAction Stop } catch { continue }
        if ($doc.api_layer.name -eq $expectedName) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "$Backend was not found. Install it from the upstream project, or select its JSON manifest."
}
function Get-LmuImplicitDisables {
    param([string]$Backend,[string[]]$AdditionalManifests=@())
    $variables=[Collections.Generic.List[string]]::new()
    foreach($name in @('DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated','DISABLE_QUAD_VIEWS_FOVEATED','DISABLE_XR_APILAYER_DIENERTECH_VECTORXR')){$variables.Add($name)}
    if($Backend -eq 'QVFR'){[void]$variables.Remove('DISABLE_XR_APILAYER_DIENERTECH_VECTORXR')}
    if($Backend -eq 'VectorXR'){$variables.Add('DISABLE_XR_APILAYER_MBUCCHIA_toolkit')}
    $manifests=[Collections.Generic.List[string]]::new()
    foreach($path in $AdditionalManifests){if($path){$manifests.Add($path)}}
    foreach($hive in @('HKLM','HKCU')) {
        $key=Get-Item -LiteralPath "${hive}:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" -ErrorAction SilentlyContinue
        if($key){foreach($path in $key.GetValueNames()){if($path.EndsWith('.json',[StringComparison]::OrdinalIgnoreCase) -and $key.GetValue($path) -eq 0){$manifests.Add($path)}}}
    }
    foreach($path in ($manifests|Select-Object -Unique)) {
        if(!(Test-Path -LiteralPath $path -PathType Leaf)){continue}
        try{$layer=([IO.File]::ReadAllText($path)|ConvertFrom-Json -ErrorAction Stop).api_layer}catch{continue}
        $marker=switch($layer.name){
            'XR_APILAYER_MBUCCHIA_quad_views_foveated' {'quad'}
            'XR_APILAYER_DIENERTECH_VECTORXR' {if($Backend -ne 'QVFR'){'vectorxr'}}
            'XR_APILAYER_MBUCCHIA_toolkit' {if($Backend -eq 'VectorXR'){'toolkit'}}
        }
        if(!$marker){continue}
        $variable=[string]$layer.disable_environment
        if($variable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $variable -notmatch '(?i)disable' -or $variable -notmatch "(?i)$marker"){
            throw "Cannot isolate installed layer $($layer.name): missing recognizable disable switch."
        }
        $variables.Add($variable)
    }
    $variables|Select-Object -Unique
}
function New-LmuLayerPlan {
    param([ValidateSet('Native','QVFR','VectorXR')][string]$Backend='Native',[Parameter(Mandatory)][string]$ManifestRoot,[string]$QuadViewsManifest,[string]$RunDirectory)
    $root=[IO.Path]::GetFullPath($ManifestRoot)
    if ($root.Contains(';')) { throw 'The package path cannot contain a semicolon (OpenXR search-path separator).' }
    if ($Backend -eq 'Native') {
        # Installed implicit QVFR would otherwise sit above our explicit adapter.
        # Selecting Native means use the runtime's implementation instead.
        $disabled=$null
        try {
            $installed=Find-LmuQuadViewsManifest
            $implicit=Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json
            $candidate=[string]$implicit.api_layer.disable_environment
            if ($candidate -match '^[A-Za-z_][A-Za-z0-9_]*$' -and $candidate -match '(?i)quad') { $disabled=$candidate }
        } catch { } # Native operation does not require a QVFR installation.
        return [pscustomobject]@{Paths=$root;Layers='XR_APILAYER_LMU_quadview';DisableVariable=$disabled;DisableVariables=@(Get-LmuImplicitDisables -Backend $Backend);ProviderManifest=$null;ExplicitManifest=$null;Library=$null;ProviderName=$null}
    }
    $expectedName=if($Backend -eq 'VectorXR'){'XR_APILAYER_DIENERTECH_VECTORXR'}else{'XR_APILAYER_MBUCCHIA_quad_views_foveated'}
    $marker=if($Backend -eq 'VectorXR'){'vectorxr'}else{'quad'}
    $manifest=Find-LmuQuadViewsManifest -Path $QuadViewsManifest -Backend $Backend
    $doc=[IO.File]::ReadAllText($manifest)|ConvertFrom-Json
    $layer=$doc.api_layer
    if (!$layer -or $layer.name -ne $expectedName) { throw "Selected file is not a $Backend layer manifest." }
    $disable=[string]$layer.disable_environment
    if (!$disable -or $disable -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $disable -notmatch '(?i)disable' -or $disable -notmatch "(?i)$marker") { throw "The $Backend manifest has no recognizable disable_environment switch; cannot safely order the layers." }
    $library=[string]$layer.library_path
    if (!$library) { throw "The $Backend manifest has no library_path." }
    if (![IO.Path]::IsPathRooted($library)) { $library=Join-Path ([IO.Path]::GetDirectoryName($manifest)) $library }
    $library=[IO.Path]::GetFullPath($library)
    if (!(Test-Path -LiteralPath $library -PathType Leaf)) { throw "$Backend library not found: $library" }
    if (!$RunDirectory) { throw "A unique run directory is required for $Backend." }
    $run=[IO.Path]::GetFullPath($RunDirectory)
    if ($run.Contains(';')) { throw 'The run path cannot contain a semicolon.' }
    if (Test-Path -LiteralPath $run) { throw 'Layer run directory already exists; refusing to overwrite it.' }
    $disabled=@(Get-LmuImplicitDisables -Backend $Backend -AdditionalManifests @($manifest))
    $layer.library_path=$library
    $layer.PSObject.Properties.Remove('disable_environment');$layer.PSObject.Properties.Remove('enable_environment')
    New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
    $explicit=Join-Path $run ($expectedName+'.json')
    [IO.File]::WriteAllText($explicit,($doc|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    [pscustomobject]@{Paths="$root;$run";Layers="XR_APILAYER_LMU_quadview;$expectedName";DisableVariable=$disable;DisableVariables=$disabled;ProviderManifest=$manifest;ExplicitManifest=$explicit;Library=$library;ProviderName=$expectedName}
}
