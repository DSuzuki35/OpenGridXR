#!/usr/bin/env bash
# Linux equivalent of package.ps1 for the unsigned cross-built binaries.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"; cd "$repo"
version="$(tr -d '\r\n ' < VERSION)"
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || { echo "Invalid VERSION" >&2; exit 1; }
out="${1:-$repo/dist}"; mkdir -p "$out"; out="$(cd "$out" && pwd)"
archive="$out/LMU-Quad-View-Mod-$version.zip"
[[ ! -e "$archive" ]] || { echo "Archive already exists: $archive" >&2; exit 1; }
stage="$(mktemp -d)"; pkg="$stage/LMU-Quad-View-Mod-$version"
mkdir -p "$pkg/build" "$pkg/licenses" "$pkg/tools" "$pkg/docs"
while read -r src dst; do [ -n "$src" ] && install -D -m 0644 "$src" "$pkg/$dst"; done <<LIST
tools/check-autostart-integrity.ps1 tools/check-autostart-integrity.ps1
docs/EAC.md docs/EAC.md
docs/INSTALLER.md docs/INSTALLER.md
docs/CPU-040.md docs/CPU-040.md
docs/CPU-RESEARCH-20261105.md docs/CPU-RESEARCH-20261105.md
mod/build/lmu_quadview.dll build/lmu_quadview.dll
mod/build/lmu_autostart.dll build/lmu_autostart.dll
mod/build/LMU-QuadView-Launcher.exe LMU-QuadView-Launcher.exe
mod/build/LMU-QuadView-Launcher.exe.config LMU-QuadView-Launcher.exe.config
PORTABLE-QVFR.txt PORTABLE-QVFR.txt
PORTABLE-VECTORXR.txt PORTABLE-VECTORXR.txt
PORTABLE-VRS.txt PORTABLE-VRS.txt
RENDER-PROFILING.txt RENDER-PROFILING.txt
BENCHMARK.md BENCHMARK.md
PORTABLE-STEREO-RESEARCH.txt PORTABLE-STEREO-RESEARCH.txt
CPU-UPLOAD-REUSE.md CPU-UPLOAD-REUSE.md
CPU-UPLOAD-FASTPATH.md CPU-UPLOAD-FASTPATH.md
PERFORMANCE-EXPERIMENT.md PERFORMANCE-EXPERIMENT.md
tools/analyze_stereo_audit.py tools/analyze_stereo_audit.py
tools/start-frame-capture.ps1 tools/start-frame-capture.ps1
tools/start-frame-capture.cmd tools/start-frame-capture.cmd
tools/check-hardware-stereo.cmd tools/check-hardware-stereo.cmd
tools/check-hardware-vrs.cmd tools/check-hardware-vrs.cmd
mod/build/tests/nv_vrs_hardware_test.exe tools/VrsHardwareCheck.exe
TODO.md TODO.md
mod/XR_APILAYER_LMU_quadview.json XR_APILAYER_LMU_quadview.json
README.md README.md
README.txt README.txt
VERSION VERSION
CHANGELOG.md CHANGELOG.md
CONTRIBUTING.md CONTRIBUTING.md
docs/ARCHITECTURE.md docs/ARCHITECTURE.md
docs/PERFORMANCE.md docs/PERFORMANCE.md
docs/focus-validation.md docs/focus-validation.md
docs/STEAM-LAUNCH.md docs/STEAM-LAUNCH.md
docs/flicker-diagnosis-20261103.md docs/flicker-diagnosis-20261103.md
docs/OPENGRIDXR-INTEGRATION.md docs/OPENGRIDXR-INTEGRATION.md
PORTABLE-DESIGN.md PORTABLE-DESIGN.md
STEREO-SHADER-CONVERSION.md STEREO-SHADER-CONVERSION.md
INSTANCED-STEREO-HANDOFF.md INSTANCED-STEREO-HANDOFF.md
LICENSE.md LICENSE.md
simhub/QuadViewTelemetry.cs simhub/QuadViewTelemetry.cs
simhub/QuadViewSimHubPlugin.cs simhub/QuadViewSimHubPlugin.cs
simhub/build-simhub-plugin.ps1 simhub/build-simhub-plugin.ps1
simhub/README.md simhub/README.md
THIRD_PARTY_NOTICES.md THIRD_PARTY_NOTICES.md
mod/vendor/minhook-1.3.4/LICENSE.txt licenses/MinHook-LICENSE.txt
prototype/third_party/openxr/LICENSE licenses/OpenXR-LICENSE.txt
mod/build/tests/stereo_instancing_test.exe tools/PortableStereoCheck.exe
LIST
awk '{print} /^\\\*+\//{exit}' mod/vendor/nvapi/nvapi.h > "$pkg/licenses/NVAPI-LICENSE.txt"
(cd "$pkg" && find . -type f ! -name SHA256.txt -print0 | sort -z | xargs -0 sha256sum | sed 's|  \./|  |' > SHA256.txt)
(cd "$pkg" && zip -q -r -X "$out/LMU-Quad-View-Mod-$version.zip" .)
rm -rf "$stage"; echo "$out/LMU-Quad-View-Mod-$version.zip"
