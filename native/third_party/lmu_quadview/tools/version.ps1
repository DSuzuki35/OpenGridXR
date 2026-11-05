# VERSION is the source of truth for release names and Windows binary metadata.
function Get-LmuVersion {
    $version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim()
    if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'VERSION must contain a release version in MAJOR.MINOR.PATCH format.'
    }
    foreach ($part in $version.Split('.')) {
        if ([decimal]$part -gt 65534) { throw 'Version components must fit Windows assembly metadata (0-65534).' }
    }
    return $version
}

function Write-LmuVersionFiles([string]$OutputDirectory) {
    $version = Get-LmuVersion
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $assemblyPath = Join-Path $OutputDirectory 'VersionInfo.cs'
    $resourcePath = Join-Path $OutputDirectory 'version.rc'
    $numericVersion = $version.Replace('.', ',') + ',0'
    @"
// Generated from VERSION. Do not edit.
using System.Reflection;
[assembly: AssemblyVersion("$version.0")]
[assembly: AssemblyFileVersion("$version.0")]
[assembly: AssemblyInformationalVersion("$version")]
"@ | Set-Content -LiteralPath $assemblyPath -Encoding ASCII
    @"
// Generated from VERSION. Do not edit.
#include <winver.h>
1 VERSIONINFO
FILEVERSION $numericVersion
PRODUCTVERSION $numericVersion
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_DLL
FILESUBTYPE 0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "LMU Quad View OpenXR layer"
            VALUE "FileVersion", "$version.0"
            VALUE "ProductName", "LMU Quad View Mod"
            VALUE "ProductVersion", "$version"
            VALUE "OriginalFilename", "lmu_quadview.dll"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
"@ | Set-Content -LiteralPath $resourcePath -Encoding ASCII
    return [PSCustomObject]@{ Version = $version; AssemblyPath = $assemblyPath; ResourcePath = $resourcePath }
}
