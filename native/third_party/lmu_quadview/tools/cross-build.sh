#!/usr/bin/env bash
# Cross-build the LMU Quad View binaries on Linux with MinGW-w64 and Mono.
# Mirrors mod/build.ps1, mod/build-msvc.ps1 (autostart DLL) and mod/build-launcher.ps1.
# Output is unsigned. Requires: g++-mingw-w64-x86-64-posix, mono-devel.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
mod="$repo/mod"; build="$mod/build"; gen="$build/generated"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
CC=${CC:-x86_64-w64-mingw32-gcc-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
version="$(tr -d '\r\n ' < "$repo/VERSION")"
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || { echo "VERSION must be MAJOR.MINOR.PATCH" >&2; exit 1; }
numeric="${version//./,},0"
mkdir -p "$build" "$gen"

version_rc() { # $1 description, $2 original filename, $3 file type
cat <<EOF
#include <winver.h>
1 VERSIONINFO
FILEVERSION $numeric
PRODUCTVERSION $numeric
FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
FILEFLAGS 0
FILEOS VOS_NT_WINDOWS32
FILETYPE $3
FILESUBTYPE 0
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "FileDescription", "$1"
            VALUE "FileVersion", "$version.0"
            VALUE "ProductName", "LMU Quad View Mod"
            VALUE "ProductVersion", "$version"
            VALUE "OriginalFilename", "$2"
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
EOF
}

echo "== native resources ($version)"
version_rc "LMU Quad View OpenXR layer" "lmu_quadview.dll" VFT_DLL > "$gen/layer.rc"
version_rc "LMU Quad View automatic startup" "lmu_autostart.dll" VFT_DLL > "$gen/autostart.rc"
"$WINDRES" "$gen/layer.rc" -O coff -o "$build/version.o"
"$WINDRES" "$gen/autostart.rc" -O coff -o "$build/autostart_version.o"

FLAGS=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-misleading-indentation -Werror -static)
cd "$mod"
version_rc "LMU Quad View VRS hardware check" "VrsHardwareCheck.exe" VFT_APP > "$gen/vrs-check.rc"
"$WINDRES" "$gen/vrs-check.rc" -O coff -o "$build/vrs-check.o"
echo "== MinHook"
for part in buffer hook trampoline; do "$CC" -O2 -c "vendor/minhook-1.3.4/src/$part.c" -o "$build/mh_$part.o"; done
"$CC" -O2 -c vendor/minhook-1.3.4/src/hde/hde64.c -o "$build/mh_hde64.o"
HOOKS=("$build/mh_buffer.o" "$build/mh_hook.o" "$build/mh_trampoline.o" "$build/mh_hde64.o")

echo "== lmu_quadview.dll"
"$CXX" "${FLAGS[@]}" -shared -s -I ../prototype/third_party xr_layer.cpp async_log.cpp engine_bridge.cpp nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp \
    "${HOOKS[@]}" "$build/version.o" -ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -o "$build/lmu_quadview.dll"
echo "== lmu_autostart.dll"
"$CXX" "${FLAGS[@]}" -Wno-cast-function-type -shared -s -I ../prototype/third_party autoload.cpp "$build/autostart_version.o" \
    -ladvapi32 -lole32 -lbcrypt -o "$build/lmu_autostart.dll"

if [[ "${SKIP_TESTS:-0}" != 1 ]]; then
  echo "== native tests (compile; mirrors build-msvc.ps1)"
  mkdir -p "$build/tests"
  # Tests are portability-sensitive across MinGW distributions; the shipped DLLs keep -Werror.
  TF=(-std=c++17 -O2 -static -w -I ../prototype/third_party)
  LIBS=(-ld3d11 -ld3dcompiler -ldxguid -ladvapi32 -luser32 -lole32 -lbcrypt)
  basic=(math focus_box native_xr_views gpu_timing render_profile swapchain_sampling pipeline_stats benchmark_control vrs_scope_policy nv_vrs ui_d3d_state focus_overlay outer_resample native_outer_texture native_outer_bindings stereo_instancing stereo_audit_abi stereo_audit stereo_shader stereo_draw_proof native_upload_reuse native_upload_batch focus_image_lease
         benchmark_timing_ownership render_view_snapshot frame_submission late_begin_frame view_crop benchmark_native benchmark_overlay benchmark_frame benchmark_gpu vrs_foveation vrs_gaze native_outer_bank
         input_validation async_log native_pair_pass nv_stereo_shader nv_multiview_backend xr_active_state diagnostic_sampling upload_reuse_toggle outer_runtime_lock end_frame_lock locate_runtime_lock app_image_lock
         autoload_policy autoload_activation autoload_verification cpu_cache_affinity)
  withlog=" input_validation focus_image_lease frame_submission render_view_snapshot late_begin_frame benchmark_frame benchmark_timing_ownership xr_active_state outer_runtime_lock end_frame_lock locate_runtime_lock app_image_lock "
  for t in "${basic[@]}"; do
    srcs=("tests/${t}_test.cpp")
    case "$t" in nv_vrs) srcs+=(nv_vrs.cpp);; stereo_shader) srcs+=(stereo_shader.cpp);; stereo_draw_proof) srcs+=(stereo_shader.cpp stereo_draw_proof.cpp);;
      async_log|nv_stereo_shader|nv_multiview_backend) srcs+=("$t.cpp");; stereo_audit) srcs+=(stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp "${HOOKS[@]}");; esac
    [[ "$withlog" == *" $t "* ]] && srcs+=(async_log.cpp)
    "$CXX" "${TF[@]}" "${srcs[@]}" "${LIBS[@]}" -o "$build/tests/${t}_test.exe"
  done
  "$CXX" "${TF[@]}" -shared tests/async_log_lifetime_fixture.cpp async_log.cpp -o "$build/tests/async_log_lifetime_fixture.dll"
  # MinGW's <stdio.h> still declares gets(); MSVC removed it. Compile a renamed copy.
  sed 's/\bgets\b/getCalls/g' tests/autoload_passthrough_test.cpp > tests/.gen_autoload_passthrough_test.cpp
  "$CXX" "${TF[@]}" tests/.gen_autoload_passthrough_test.cpp autoload.cpp "${LIBS[@]}" -o "$build/tests/autoload_passthrough_test.exe"
  rm -f tests/.gen_autoload_passthrough_test.cpp
  for t in engine_hooks native_outer_fallback benchmark_engine; do
    "$CXX" "${TF[@]}" "tests/${t}_test.cpp" nv_vrs.cpp stereo_audit.cpp stereo_shader.cpp stereo_draw_proof.cpp "${HOOKS[@]}" "${LIBS[@]}" -o "$build/tests/${t}_test.exe"
  done
  "$CXX" "${TF[@]}" xr_layer.cpp xr_layer_tests.cpp async_log.cpp "${LIBS[@]}" -o "$build/tests/xr_layer_tests.exe"
  # Shipped as tools/VrsHardwareCheck.exe: real-driver NVIDIA VRS verification.
  "$CXX" "${FLAGS[@]}" tests/nv_vrs_hardware_test.cpp nv_vrs.cpp "$build/vrs-check.o" -I ../prototype/third_party "${LIBS[@]}" -ldxgi -o "$build/tests/nv_vrs_hardware_test.exe"
fi

echo "== LMU-QuadView-Launcher.exe"
cat > "$gen/VersionInfo.cs" <<EOF2
// Generated from VERSION. Do not edit.
using System.Reflection;
[assembly: AssemblyVersion("$version.0")]
[assembly: AssemblyFileVersion("$version.0")]
[assembly: AssemblyInformationalVersion("$version")]
EOF2
version_rc "LMU Quad View Launcher" "LMU-QuadView-Launcher.exe" VFT_APP > "$gen/launcher.rc"
cp launcher/app.manifest "$gen/app.manifest"; cp launcher/assets/quadview.ico "$gen/quadview.ico"
printf '32512 ICON "quadview.ico"\n1 24 "app.manifest"\n' >> "$gen/launcher.rc"
(cd "$gen" && "$WINDRES" launcher.rc -O res -o launcher.res)
CORE=(launcher/QvfrSettings.cs launcher/SettingRanges.cs launcher/FocusGeometry.cs launcher/LaunchService.cs launcher/AssemblyInfo.cs "$gen/VersionInfo.cs"
      launcher/BenchmarkController.cs launcher/JoystickInput.cs launcher/LaunchDiagnostics.cs launcher/SteamLaunch.cs launcher/AutoStart.cs launcher/RegistryRestoreScope.cs
      launcher/ProfileStore.cs launcher/UpdateService.cs launcher/ExternalCommands.cs)
UI=(launcher/Launcher.cs launcher/NativeChrome.cs launcher/LauncherUpdates.cs)
REFS=(-r:System.dll -r:System.Core.dll -r:System.Xml.dll -r:System.Windows.Forms.dll -r:System.Drawing.dll -r:System.Xml.Linq.dll -r:System.Web.Extensions.dll -r:System.Security.dll)
mcs -nologo -sdk:4.8 -target:winexe -platform:x64 -optimize+ -warnaserror+ -win32res:"$gen/launcher.res" \
    -resource:launcher/assets/quadview.ico,LmuQuadView.Icon "${REFS[@]}" "${CORE[@]}" "${UI[@]}" -out:"$build/LMU-QuadView-Launcher.exe"
cp launcher/app.config "$build/LMU-QuadView-Launcher.exe.config"
if [[ "${SKIP_TESTS:-0}" != 1 ]]; then
  echo "== launcher tests (compile)"
  for t in ExternalCommandsTests PortableChecksTests UpdateServiceTests ProfileStoreTests RegistrySafetyTests HardeningTests SettingsTests FocusGeometryTests LaunchServiceTests SteamLaunchTests AutoStartTests ProcessPathTests JoystickInputTests LaunchDiagnosticsTests BenchmarkTests; do
    mcs -nologo -sdk:4.8 -target:exe -platform:x64 -optimize+ "${REFS[@]}" -main:"$t" "${CORE[@]}" "launcher/$t.cs" -out:"$build/tests/$t.exe"
  done
  mcs -nologo -sdk:4.8 -target:exe -platform:x64 -optimize+ "${REFS[@]}" -main:SliderRangeTests "${CORE[@]}" "${UI[@]}" launcher/SliderRangeTests.cs -out:"$build/tests/SliderRangeTests.exe"
fi
mkdir -p "$build/tests"
echo "== SimHub telemetry reader (plugin itself builds on a PC with SimHub)"
"$CXX" -std=c++17 -O2 -static "$repo/simhub/tests/telemetry_fixture.cpp" -o "$build/tests/telemetry_fixture.exe"
mcs -nologo -sdk:4.8 -target:exe -warnaserror+ -r:System.dll -r:System.Core.dll "$repo/simhub/QuadViewTelemetry.cs" "$repo/simhub/tests/TelemetryTests.cs" -out:"$build/tests/TelemetryTests.exe"
echo "== done"
sha256sum "$build/lmu_quadview.dll" "$build/lmu_autostart.dll" "$build/LMU-QuadView-Launcher.exe"
