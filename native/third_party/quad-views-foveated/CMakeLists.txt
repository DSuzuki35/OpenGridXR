# Built-in Quad-Views-Foveated provider for LMU Quad View.
# Upstream: https://github.com/mbucchia/Quad-Views-Foveated (MIT), see NOTICE-LMU.md.
# Replaces upstream's Visual Studio project: same sources, defines, shaders and
# generated dispatch code, static CRT, x64 only. The DLL keeps the upstream layer
# name so the existing QVFR settings.cfg profile and adapter contract are unchanged.
cmake_minimum_required(VERSION 3.24)
project(QuadViewsFoveated VERSION 1.1.4 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Quad-Views-Foveated is a Windows x64 MSVC (D3D11/D3D12) layer.")
endif()

set(QVFR_LAYER_NAME "XR_APILAYER_MBUCCHIA_quad_views_foveated")
set(QVFR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/openxr-api-layer")
set(QVFR_EXT "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(QVFR_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${QVFR_GEN}")

# Dispatch code generated from the OpenXR registry, as upstream's pre-build step.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT "${QVFR_SRC}/framework/dispatch.gen.cpp" "${QVFR_SRC}/framework/dispatch.gen.h"
  COMMAND Python3::Interpreter "${QVFR_SRC}/framework/dispatch_generator.py"
  DEPENDS "${QVFR_SRC}/framework/dispatch_generator.py" "${QVFR_SRC}/framework/layer_apis.py"
          "${QVFR_EXT}/OpenXR-SDK-Source/specification/registry/xr.xml"
  WORKING_DIRECTORY "${QVFR_SRC}/framework"
  COMMENT "Generating Quad-Views-Foveated OpenXR dispatch")

# HLSL -> headers with g_<Name> byte arrays (upstream FxCompile settings).
find_program(QVFR_FXC fxc HINTS
  "$ENV{WindowsSdkVerBinPath}/x64"
  "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
  "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
  "C:/Program Files (x86)/Windows Kits/10/bin/x64"
  REQUIRED)
set(QVFR_SHADER_HEADERS)
function(qvfr_shader name profile)
  set(extra ${ARGN})
  add_custom_command(
    OUTPUT "${QVFR_GEN}/${name}.h"
    COMMAND "${QVFR_FXC}" /nologo /T ${profile} /E main /Vn g_${name} /Fh "${QVFR_GEN}/${name}.h" ${extra} "${QVFR_SRC}/${name}.hlsl"
    DEPENDS "${QVFR_SRC}/${name}.hlsl"
    COMMENT "Compiling ${name}.hlsl")
  set(QVFR_SHADER_HEADERS ${QVFR_SHADER_HEADERS} "${QVFR_GEN}/${name}.h" PARENT_SCOPE)
endfunction()
qvfr_shader(ProjectionVS vs_5_0)
qvfr_shader(ProjectionPS ps_5_0)
qvfr_shader(SharpeningCS cs_5_0 /I "${QVFR_EXT}/FidelityFX-CAS/ffx-cas"
  /D CAS_SAMPLE_FP16=0 /D CAS_SAMPLE_SHARPEN_ONLY=1 /D WIDTH=64 /D HEIGHT=1 /D DEPTH=1)

add_library(${QVFR_LAYER_NAME} SHARED
  "${QVFR_SRC}/framework/dispatch.cpp"
  "${QVFR_SRC}/framework/dispatch.gen.cpp"
  "${QVFR_SRC}/framework/entry.cpp"
  "${QVFR_SRC}/framework/log.cpp"
  "${QVFR_SRC}/layer.cpp"
  "${QVFR_SRC}/utils/composition.cpp"
  "${QVFR_SRC}/utils/d3d11.cpp"
  "${QVFR_SRC}/utils/d3d12.cpp"
  "${QVFR_SRC}/utils/general.cpp"
  "${QVFR_SRC}/utils/input.cpp"
  "${QVFR_EXT}/fmt/src/format.cc"
  "${QVFR_SRC}/module.def"
  "${QVFR_SRC}/resource.rc"
  ${QVFR_SHADER_HEADERS})
set_source_files_properties("${QVFR_EXT}/fmt/src/format.cc" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_precompile_headers(${QVFR_LAYER_NAME} PRIVATE "${QVFR_SRC}/pch.h")
target_include_directories(${QVFR_LAYER_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../lmu_quadview/mod"
  "${QVFR_GEN}" "${QVFR_SRC}" "${QVFR_SRC}/framework"
  "${QVFR_EXT}/fmt/include"
  "${QVFR_EXT}/OpenXR-SDK/include" "${QVFR_EXT}/OpenXR-SDK/src/common"
  "${QVFR_EXT}/OpenXR-MixedReality/shared/XrUtility" "${QVFR_EXT}/OpenXR-MixedReality/shared/SampleShared"
  "${QVFR_EXT}/FidelityFX-CAS/ffx-cas"
  "${QVFR_EXT}/wil/include")
target_compile_definitions(${QVFR_LAYER_NAME} PRIVATE
  LAYER_NAME="${QVFR_LAYER_NAME}" UNICODE _UNICODE _WINDOWS _USRDLL _CRT_SECURE_NO_WARNINGS $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
target_compile_options(${QVFR_LAYER_NAME} PRIVATE /W3 /EHsc /utf-8)
target_link_libraries(${QVFR_LAYER_NAME} PRIVATE d3d11 d3d12 dxgi advapi32 shell32 ole32 user32)
target_link_options(${QVFR_LAYER_NAME} PRIVATE /DEPENDENTLOADFLAG:0x800)

# Package layout: providers/QVFR/{DLL, manifest, default settings.cfg}.
set(QVFR_OUT "${CMAKE_BINARY_DIR}/$<CONFIG>/providers/qvfr")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/XR_APILAYER_MBUCCHIA_quad_views_foveated.json"
  "${CMAKE_CURRENT_BINARY_DIR}/${QVFR_LAYER_NAME}.json" COPYONLY)
add_custom_target(qvfr-package ALL
  DEPENDS ${QVFR_LAYER_NAME}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${QVFR_OUT}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${QVFR_LAYER_NAME}> "${QVFR_OUT}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${QVFR_LAYER_NAME}.json" "${QVFR_OUT}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/settings.cfg" "${QVFR_OUT}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${QVFR_OUT}/LICENSE-Quad-Views-Foveated.txt")
