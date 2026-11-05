$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static layout_check.cpp -o build/layout_check.exe
    if ($LASTEXITCODE -ne 0) { throw 'Reconstructed layout checks failed' }
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -municode -static -I third_party openxr_probe.cpp -ladvapi32 -o build/openxr_probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'Compiler failed' }
    & .\build\openxr_probe.exe --self-test
    if ($LASTEXITCODE -ne 0) { throw 'Probe self-test failed' }
} finally {
    Pop-Location
}
