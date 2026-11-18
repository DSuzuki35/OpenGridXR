add_library(
    depthxr_core
    STATIC
    src/config_parser.cpp
    src/input_devices.cpp
    src/quadviews_compose.cpp
    src/quadviews_sizing.cpp
    src/runtime_pacing.cpp
    src/runtime_relay.cpp
    src/external_status.cpp
    src/runtime_compatibility.cpp
    src/seen_apps.cpp
    src/turbo_trace.cpp
    src/turbo_metrics.cpp
    src/turbo_recovery.cpp
    src/settings.cpp
    src/sound_player.cpp
    src/settings_resolver.cpp
    src/session_graphics.cpp
    src/performance_monitor.cpp
    src/osd_strings.cpp
    src/swapchain_state.cpp
    src/thread_census.cpp
    src/util/config_path.cpp
    src/util/logger.cpp
    src/util/process_info.cpp
    src/util/process_exit.cpp
    src/util/atomic_file.cpp
    src/math/stereo_boost.cpp
    src/math/convergence.cpp
    src/math/depth_submission.cpp
    src/math/pivot_step.cpp
    src/math/pivot_view.cpp
    src/math/pivot_yaw.cpp
)

target_include_directories(depthxr_core PUBLIC include)
# The OSD's translations are UTF-8 literals.
if(MSVC)
    set_source_files_properties(src/osd_strings.cpp PROPERTIES COMPILE_OPTIONS /utf-8)
endif()
target_compile_features(depthxr_core PUBLIC cxx_std_20)
if(WIN32)
    target_sources(depthxr_core PRIVATE src/util/crt_failure_report.cpp src/performance_sampler.cpp)
    target_link_libraries(depthxr_core PUBLIC dinput8 dxguid ole32 winmm cfgmgr32)
endif()

if(WIN32 AND DEPTHXR_BUILD_LAYER)
    add_library(opengrid_openxr_headers INTERFACE)
    target_include_directories(opengrid_openxr_headers INTERFACE "${PROJECT_SOURCE_DIR}/../openxr")
    add_library(OpenXR::openxr_loader ALIAS opengrid_openxr_headers)
    set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(vulkan_headers
        URL https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/b379292b2ab6df5771ba9870d53cf8b2c9295daf
        URL_HASH SHA256=2ad177c0e468ac2eb3db9b5be6b8a18a6730d339378ac670e5fbda3503ac7d64
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(vulkan_headers)
    add_library(depthxr_osd STATIC src/osd_renderer.cpp src/osd_graphics.cpp src/osd_vulkan.cpp)
    target_include_directories(depthxr_osd PUBLIC include)
    target_compile_features(depthxr_osd PUBLIC cxx_std_20)
    target_compile_definitions(depthxr_osd PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_GRAPHICS_API_VULKAN)
    target_link_libraries(depthxr_osd PUBLIC depthxr_core OpenXR::openxr_loader Vulkan::Headers d3d11 d3d12 d3dcompiler gdi32)
    target_sources(depthxr_osd PRIVATE
        "${PROJECT_SOURCE_DIR}/../../src/overlay_transport.cpp"
        "${PROJECT_SOURCE_DIR}/../../src/overlay_renderer.cpp"
        "${PROJECT_SOURCE_DIR}/../../src/desktop_interaction.cpp"
        "${PROJECT_SOURCE_DIR}/../../wheels/mesh_renderer.cpp")
    target_sources(depthxr_osd PRIVATE "${PROJECT_SOURCE_DIR}/../../src/helmet_renderer.cpp" "${PROJECT_SOURCE_DIR}/../../src/helmet_light.cpp" "${PROJECT_SOURCE_DIR}/../../src/helmet_math.cpp" "${PROJECT_SOURCE_DIR}/../../telemetry/telemetry.cpp")
    target_include_directories(depthxr_osd PRIVATE "${PROJECT_SOURCE_DIR}/../../telemetry")
    target_link_libraries(depthxr_osd PUBLIC windowscodecs ole32 dxgi)
    target_include_directories(depthxr_osd PUBLIC "${PROJECT_SOURCE_DIR}/../../include")
    target_sources(depthxr_osd PRIVATE src/session_graphics_query.cpp src/spacewarp_probe.cpp src/handshake_census.cpp)

    add_library(
        XR_APILAYER_OPENGRIDXR_ENHANCEMENTS
        SHARED
        src/dll_main.cpp
        src/layer_entry.cpp
        src/openxr_layer.cpp
    )

    target_include_directories(XR_APILAYER_OPENGRIDXR_ENHANCEMENTS PRIVATE include "${PROJECT_SOURCE_DIR}/../../include")
    target_link_libraries(
        XR_APILAYER_OPENGRIDXR_ENHANCEMENTS
        PRIVATE
        depthxr_core
        depthxr_osd
        OpenXR::openxr_loader
        d3d11
        d3dcompiler
        dxgi
        shlwapi
    )
    set(_vectorxr_version "${PROJECT_VERSION}")
    message(STATUS "OpenGrid XR private engine version: ${_vectorxr_version}")

    target_compile_definitions(
        XR_APILAYER_OPENGRIDXR_ENHANCEMENTS
        PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        XR_USE_GRAPHICS_API_D3D11
        OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}"
    )

    set_target_properties(XR_APILAYER_OPENGRIDXR_ENHANCEMENTS PROPERTIES OUTPUT_NAME "OpenGridXR-Enhancements")
    add_custom_command(TARGET XR_APILAYER_OPENGRIDXR_ENHANCEMENTS POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:XR_APILAYER_OPENGRIDXR_ENHANCEMENTS>
       "${PROJECT_SOURCE_DIR}/../../build/$<CONFIG>/OpenGridXR-Enhancements.dll")
    option(DEPTHXR_REQUIRE_HW_TESTS "Require hardware for GPU probe tests" OFF)

    if(DEPTHXR_BUILD_HARDWARE_PROBES)
        add_executable(
            vectorxr_hardware_probe
            tools/pivot_space_probe.cpp
            tools/probe_gpu_load.cpp
            tools/probe_d3d11.cpp
            tools/probe_vulkan.cpp
            tools/probe_render_check.cpp
            tools/probe_vram_ballast.cpp
            tools/probe_common.h
            tests/test_adapter.h
        )
        # FG_TEST_ADAPTER's helper is shared with the GPU tests.
        target_include_directories(vectorxr_hardware_probe PRIVATE tests)
        # Vulkan is loaded at run time (vulkan-1.dll): only its headers are linked.
        target_link_libraries(
            vectorxr_hardware_probe
            PRIVATE
            OpenXR::openxr_loader
            Vulkan::Headers
            d3d11
            d3d12
            d3dcompiler
            dxgi
        )
        target_compile_features(vectorxr_hardware_probe PRIVATE cxx_std_20)
        target_compile_definitions(
            vectorxr_hardware_probe
            PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            XR_USE_PLATFORM_WIN32
            XR_USE_GRAPHICS_API_D3D11
            XR_USE_GRAPHICS_API_VULKAN
            VK_NO_PROTOTYPES
        )
        if(MSVC)
            target_compile_options(vectorxr_hardware_probe PRIVATE /W4 /permissive-)
        endif()
        # --graphics vulkan --gpu-ms: the GPU load as SPIR-V from the pinned DXC (without it the Vulkan mode runs
        # without --gpu-ms).
        if(DEPTHXR_DXC)
            set(_probe_shaders "${CMAKE_CURRENT_BINARY_DIR}/probe_shaders")
            file(MAKE_DIRECTORY "${_probe_shaders}")
            add_custom_command(
                OUTPUT "${_probe_shaders}/probe_vulkan_load.h"
                COMMAND "${DEPTHXR_DXC}" -nologo -spirv -T cs_6_0 -E CSMain -O3 -WX -fspv-target-env=vulkan1.0
                        -Vn kProbeLoadSpirv -Fh "${_probe_shaders}/probe_vulkan_load.h"
                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/probe_vulkan_load.hlsl"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/probe_vulkan_load.hlsl"
                COMMENT "Compiling the hardware probe's Vulkan GPU load (SPIR-V)"
                VERBATIM)
            target_sources(vectorxr_hardware_probe PRIVATE "${_probe_shaders}/probe_vulkan_load.h")
            target_include_directories(vectorxr_hardware_probe PRIVATE "${_probe_shaders}")
            target_compile_definitions(vectorxr_hardware_probe PRIVATE OPENGRIDXR_ENHANCEMENTS_PROBE_SPIRV=1)
        endif()
        # --vulkan-validation without VK_ADD_LAYER_PATH: the validation layer manifest the tests' configuration
        # writes into this build tree, when there is one.
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/vulkan_layers" _probe_vk_layer_dir)
        string(REPLACE "\\" "\\\\" _probe_vk_layer_dir "${_probe_vk_layer_dir}")
        target_compile_definitions(vectorxr_hardware_probe PRIVATE
            "OPENGRIDXR_ENHANCEMENTS_PROBE_VULKAN_LAYER_DIR=\"${_probe_vk_layer_dir}\"")
        add_custom_command(
            TARGET vectorxr_hardware_probe
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:OpenXR::openxr_loader>
                    $<TARGET_FILE_DIR:vectorxr_hardware_probe>
        )

        if(DEPTHXR_BUILD_TESTS)
            add_test(
                NAME vectorxr_hardware_probe_self_test
                COMMAND vectorxr_hardware_probe --self-test
            )
            set_tests_properties(vectorxr_hardware_probe_self_test PROPERTIES TIMEOUT 10)
            # The rendering half on this machine's GPU, without OpenXR: Direct3D 11 with its debug layer, Vulkan
            # validated (with synchronization validation) when the tests have a validation layer.
            set(_probe_validation "")
            if(DEPTHXR_VULKAN_VALIDATION_LAYER OR
               (DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Bin/VkLayer_khronos_validation.dll"))
                set(_probe_validation --vulkan-validation)
            endif()
            add_test(NAME vectorxr_hardware_probe_render_check_d3d11
                     COMMAND vectorxr_hardware_probe --render-check --d3d11-debug)
            add_test(NAME vectorxr_hardware_probe_render_check_vulkan
                     COMMAND vectorxr_hardware_probe --render-check --graphics vulkan ${_probe_validation})
            add_test(NAME vectorxr_hardware_probe_render_check_vulkan_late_1_0
                     COMMAND vectorxr_hardware_probe --render-check --graphics vulkan --vk-late-submit
                             --vulkan-api 1.0 ${_probe_validation})
            # The checks of the checks: a VkFence left undestroyed must fail the render check (validation reports it
            # at vkDestroyDevice, before the summary), and a queue that never goes idle must end the run within its
            # 10 s waits instead of hanging (then the check releases it, so no process exits with a stalled queue).
            set(_probe_render_checks vectorxr_hardware_probe_render_check_d3d11
                vectorxr_hardware_probe_render_check_vulkan vectorxr_hardware_probe_render_check_vulkan_late_1_0
                vectorxr_hardware_probe_render_check_vulkan_stuck_queue)
            add_test(NAME vectorxr_hardware_probe_render_check_vulkan_stuck_queue
                     COMMAND vectorxr_hardware_probe --render-check --graphics vulkan --vk-test-stuck-queue)
            string(CONCAT _probe_stuck_output "did not finish within 10 s before the swapchains are destroyed"
                   ".*the queue ran on.*did not finish on the GPU within 10 s")
            set_tests_properties(vectorxr_hardware_probe_render_check_vulkan_stuck_queue PROPERTIES
                                 PASS_REGULAR_EXPRESSION "${_probe_stuck_output}")
            if(_probe_validation)
                add_test(NAME vectorxr_hardware_probe_render_check_vulkan_leak
                         COMMAND vectorxr_hardware_probe --render-check --graphics vulkan --vulkan-validation
                                 --vk-test-leak)
                string(CONCAT _probe_leak_output "vkDestroyDevice-device-05137.*VkFence"
                       ".*0 failed, 1 debug/validation messages: FAILED")
                set_tests_properties(vectorxr_hardware_probe_render_check_vulkan_leak PROPERTIES
                                     PASS_REGULAR_EXPRESSION "${_probe_leak_output}")
                list(APPEND _probe_render_checks vectorxr_hardware_probe_render_check_vulkan_leak)
            endif()
            # A machine without vulkan-1.dll, a hardware GPU, the Direct3D 11 SDK layers or the validation layer
            # skips them (77), unless the tests require the hardware.
            set_tests_properties(${_probe_render_checks} PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
            if(DEPTHXR_REQUIRE_HW_TESTS)
                set_tests_properties(${_probe_render_checks} PROPERTIES ENVIRONMENT "OPENGRIDXR_ENHANCEMENTS_TESTS_REQUIRE_HW=1")
            endif()
            add_test(NAME vectorxr_hardware_probe_render_check_unavailable
                     COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:vectorxr_hardware_probe>
                             -DEMPTY_LAYER_DIR=${CMAKE_CURRENT_BINARY_DIR}/probe_no_vulkan_layers
                             -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/probe_expect_unavailable.cmake)
            set_tests_properties(vectorxr_hardware_probe_render_check_unavailable PROPERTIES TIMEOUT 120)
        endif()
    endif()

    if(DEPTHXR_BUILD_TESTS)
        # The layer DLL imports no C or C++ runtime DLL (dumpbin sits beside the linker), without a GPU.
        get_filename_component(_layer_linker_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(DEPTHXR_DUMPBIN dumpbin HINTS "${_layer_linker_dir}")
        if(DEPTHXR_DUMPBIN)
            add_test(NAME depthxr_layer_crt_imports_tests
                     COMMAND "${CMAKE_COMMAND}" "-DDUMPBIN=${DEPTHXR_DUMPBIN}"
                             -DLAYER=$<TARGET_FILE:XR_APILAYER_OPENGRIDXR_ENHANCEMENTS>
                             -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/layer_crt_imports.cmake")
            set_tests_properties(depthxr_layer_crt_imports_tests PROPERTIES TIMEOUT 60)
        endif()
        # The layer DLL loaded, negotiated and unloaded three times, as the loader does, without loading
        # msvcp140 or vcruntime140 (it creates no graphics device).
        add_executable(depthxr_layer_load_tests tests/layer_load_tests.cpp)
        target_include_directories(depthxr_layer_load_tests PRIVATE include)
        # The OpenXR headers only: the test calls nothing in the loader.
        target_link_libraries(depthxr_layer_load_tests PRIVATE OpenXR::openxr_loader)
        target_compile_features(depthxr_layer_load_tests PRIVATE cxx_std_20)
        target_compile_definitions(depthxr_layer_load_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_dependencies(depthxr_layer_load_tests XR_APILAYER_OPENGRIDXR_ENHANCEMENTS)
        add_test(NAME depthxr_layer_load_tests
                 COMMAND depthxr_layer_load_tests $<TARGET_FILE:XR_APILAYER_OPENGRIDXR_ENHANCEMENTS>)
        set_tests_properties(depthxr_layer_load_tests PROPERTIES TIMEOUT 30)
        # The layer's fatal C runtime errors reach the process's crash reporting and the log, in child
        # processes that abort on purpose (no GPU).
        add_executable(depthxr_crt_failure_report_tests tests/crt_failure_report_tests.cpp)
        target_link_libraries(depthxr_crt_failure_report_tests PRIVATE depthxr_core)
        target_compile_definitions(depthxr_crt_failure_report_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_test(NAME depthxr_crt_failure_report_tests COMMAND depthxr_crt_failure_report_tests)
        set_tests_properties(depthxr_crt_failure_report_tests PROPERTIES TIMEOUT 60)
        # FG_TEST_ADAPTER's name matching and environment parsing (test_adapter.h), without a GPU.
        add_executable(depthxr_test_adapter_tests tests/test_adapter_tests.cpp)
        target_compile_definitions(depthxr_test_adapter_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_test(NAME depthxr_test_adapter_tests COMMAND depthxr_test_adapter_tests)
        set_tests_properties(depthxr_test_adapter_tests PROPERTIES TIMEOUT 10)
        add_executable(depthxr_osd_backend_tests tests/osd_backend_tests.cpp)
        target_link_libraries(depthxr_osd_backend_tests PRIVATE depthxr_osd dxgi)
        target_compile_definitions(depthxr_osd_backend_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 XR_USE_GRAPHICS_API_D3D12 XR_USE_GRAPHICS_API_VULKAN)
        add_test(NAME depthxr_osd_d3d12_tests COMMAND depthxr_osd_backend_tests d3d12)
        add_test(NAME depthxr_osd_vulkan_tests COMMAND depthxr_osd_backend_tests vulkan)
        set_tests_properties(depthxr_osd_d3d12_tests depthxr_osd_vulkan_tests PROPERTIES TIMEOUT 30)
        set_tests_properties(depthxr_osd_vulkan_tests PROPERTIES SKIP_RETURN_CODE 77)
        add_executable(depthxr_osd_tests tests/osd_tests.cpp)
        target_link_libraries(depthxr_osd_tests PRIVATE depthxr_osd)
        target_compile_definitions(depthxr_osd_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11)
        add_test(NAME depthxr_osd_tests COMMAND depthxr_osd_tests)
        set_tests_properties(depthxr_osd_tests PROPERTIES TIMEOUT 20)

        # Compile the real frame-loop implementation into a dedicated fake-runtime
        # harness. DEPTHXR_TESTING only grants the harness state setup/inspection
        # access; no test hooks are present in the shipping layer target.
        add_executable(
            depthxr_turbo_frame_tests
            tests/turbo_frame_tests.cpp
            tests/iracing_depth_tests.cpp
            src/openxr_layer.cpp
        )
        set_source_files_properties(tests/turbo_frame_tests.cpp PROPERTIES
            COMPILE_DEFINITIONS XR_USE_GRAPHICS_API_D3D12)
        target_include_directories(depthxr_turbo_frame_tests PRIVATE include)
        target_link_libraries(
            depthxr_turbo_frame_tests
            PRIVATE
            depthxr_core
            depthxr_osd
            OpenXR::openxr_loader
            d3d11
            d3dcompiler
            dxgi
            shlwapi
        )
        target_compile_definitions(
            depthxr_turbo_frame_tests
            PRIVATE
            DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            XR_USE_GRAPHICS_API_D3D11
            OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}"
        )
        add_test(NAME depthxr_turbo_frame_tests COMMAND depthxr_turbo_frame_tests)
        add_test(NAME opengrid_iracing_depth_tests COMMAND depthxr_turbo_frame_tests --cockpit-depth)
        set_tests_properties(depthxr_turbo_frame_tests PROPERTIES TIMEOUT 90)

        add_executable(depthxr_pivot_pose_tests tests/pivot_pose_tests.cpp src/openxr_layer.cpp)
        target_include_directories(depthxr_pivot_pose_tests PRIVATE include)
        target_link_libraries(depthxr_pivot_pose_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3dcompiler dxgi shlwapi)
        target_compile_definitions(depthxr_pivot_pose_tests PRIVATE DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}")
        add_test(NAME depthxr_pivot_pose_tests COMMAND depthxr_pivot_pose_tests)
        set_tests_properties(depthxr_pivot_pose_tests PROPERTIES TIMEOUT 15)

        add_executable(depthxr_pivot_snap_tests tests/pivot_snap_tests.cpp src/openxr_layer.cpp)
        target_include_directories(depthxr_pivot_snap_tests PRIVATE include)
        target_link_libraries(depthxr_pivot_snap_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3dcompiler dxgi shlwapi)
        target_compile_definitions(depthxr_pivot_snap_tests PRIVATE DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}")
        add_test(NAME depthxr_pivot_snap_tests COMMAND depthxr_pivot_snap_tests)
        set_tests_properties(depthxr_pivot_snap_tests PROPERTIES TIMEOUT 30)

        add_executable(depthxr_pivot_gaze_tests tests/pivot_gaze_tests.cpp src/openxr_layer.cpp src/layer_entry.cpp)
        target_include_directories(depthxr_pivot_gaze_tests PRIVATE include)
        target_link_libraries(depthxr_pivot_gaze_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3dcompiler dxgi shlwapi)
        target_compile_definitions(depthxr_pivot_gaze_tests PRIVATE DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}")
        add_test(NAME depthxr_pivot_gaze_tests COMMAND depthxr_pivot_gaze_tests)
        set_tests_properties(depthxr_pivot_gaze_tests PROPERTIES TIMEOUT 15)

        add_executable(depthxr_eye_gaze_tests tests/eye_gaze_tests.cpp src/openxr_layer.cpp)
        target_include_directories(depthxr_eye_gaze_tests PRIVATE include)
        target_link_libraries(depthxr_eye_gaze_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3dcompiler dxgi shlwapi)
        target_compile_definitions(depthxr_eye_gaze_tests PRIVATE DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}")
        add_test(NAME depthxr_eye_gaze_tests COMMAND depthxr_eye_gaze_tests)
        set_tests_properties(depthxr_eye_gaze_tests PROPERTIES TIMEOUT 15)

        # The space-warp probe through the layer's own hooks (xrCreateApiLayerInstance
        # on) against a stand-in runtime (CPU only).
        add_executable(depthxr_spacewarp_probe_tests tests/spacewarp_probe_tests.cpp src/openxr_layer.cpp
            src/layer_entry.cpp)
        target_include_directories(depthxr_spacewarp_probe_tests PRIVATE include)
        target_link_libraries(depthxr_spacewarp_probe_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3dcompiler dxgi shlwapi)
        target_compile_definitions(depthxr_spacewarp_probe_tests PRIVATE DEPTHXR_TESTING
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11 OPENGRIDXR_ENHANCEMENTS_VERSION="${_vectorxr_version}")
        add_test(NAME depthxr_spacewarp_probe_tests COMMAND depthxr_spacewarp_probe_tests)
        set_tests_properties(depthxr_spacewarp_probe_tests PROPERTIES TIMEOUT 30)
        add_executable(opengrid_lmu_external_quad_tests tests/lmu_external_quad_tests.cpp src/openxr_layer.cpp src/layer_entry.cpp)
        set_source_files_properties(tests/lmu_external_quad_tests.cpp PROPERTIES COMPILE_DEFINITIONS "XR_USE_GRAPHICS_API_D3D12")
        target_include_directories(opengrid_lmu_external_quad_tests PRIVATE include)
        target_link_libraries(opengrid_lmu_external_quad_tests PRIVATE depthxr_core depthxr_osd
            OpenXR::openxr_loader d3d11 d3d12 d3dcompiler dxgi shlwapi)
        target_compile_definitions(opengrid_lmu_external_quad_tests PRIVATE DEPTHXR_TESTING WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_GRAPHICS_API_D3D11)
        set_target_properties(opengrid_lmu_external_quad_tests PROPERTIES OUTPUT_NAME "Le Mans Ultimate" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lmu-external-fixture")
        add_test(NAME opengrid_lmu_external_quad_tests COMMAND opengrid_lmu_external_quad_tests)

    endif()
endif()

if(DEPTHXR_BUILD_TESTS)
    add_executable(
        depthxr_layer_tests
        tests/config_tests.cpp
    )

    target_link_libraries(depthxr_layer_tests PRIVATE depthxr_core)
    if(WIN32)
        target_link_libraries(depthxr_layer_tests PRIVATE d3dcompiler)
        target_compile_definitions(
            depthxr_layer_tests
            PRIVATE OPENGRIDXR_ENHANCEMENTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        )
    endif()
    add_test(NAME depthxr_layer_tests COMMAND depthxr_layer_tests)
    add_test(NAME opengrid-eye-gaze-policy COMMAND depthxr_layer_tests --eye-gaze-policy)
    # About 5 s on hosted CI; a hang must not hold the runner for its 25 minutes.
    set_tests_properties(depthxr_layer_tests PROPERTIES TIMEOUT 120)
    add_executable(depthxr_turbo_trace_tests tests/turbo_trace_tests.cpp)
    target_link_libraries(depthxr_turbo_trace_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_turbo_trace_tests COMMAND depthxr_turbo_trace_tests)
    set_tests_properties(depthxr_turbo_trace_tests PROPERTIES TIMEOUT 15)

    add_executable(depthxr_turbo_recovery_tests tests/turbo_recovery_tests.cpp)
    target_link_libraries(depthxr_turbo_recovery_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_turbo_recovery_tests COMMAND depthxr_turbo_recovery_tests)
    set_tests_properties(depthxr_turbo_recovery_tests PROPERTIES TIMEOUT 10)
    add_executable(depthxr_runtime_relay_tests tests/runtime_relay_tests.cpp)
    target_link_libraries(depthxr_runtime_relay_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_runtime_relay_tests COMMAND depthxr_runtime_relay_tests)
    set_tests_properties(depthxr_runtime_relay_tests PROPERTIES TIMEOUT 20)
    add_executable(depthxr_external_status_tests tests/external_status_tests.cpp)
    target_link_libraries(depthxr_external_status_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_external_status_tests COMMAND depthxr_external_status_tests)
    set_tests_properties(depthxr_external_status_tests PROPERTIES TIMEOUT 20)
    if(WIN32)
        add_library(depthxr_process_exit_probe SHARED tests/process_exit_probe.cpp src/dll_main.cpp)
        target_link_libraries(depthxr_process_exit_probe PRIVATE depthxr_core)
        add_executable(depthxr_process_exit_tests tests/process_exit_tests.cpp)
        add_dependencies(depthxr_process_exit_tests depthxr_process_exit_probe)
        add_test(NAME depthxr_process_exit_tests COMMAND depthxr_process_exit_tests)
        set_tests_properties(depthxr_process_exit_tests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(depthxr_path_unicode_tests tests/path_unicode_tests.cpp)
    target_link_libraries(depthxr_path_unicode_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_path_unicode_tests COMMAND depthxr_path_unicode_tests)
    set_tests_properties(depthxr_path_unicode_tests PROPERTIES TIMEOUT 10)
    add_executable(depthxr_sound_player_tests tests/sound_player_tests.cpp)
    target_link_libraries(depthxr_sound_player_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_sound_player_tests COMMAND depthxr_sound_player_tests)
    set_tests_properties(depthxr_sound_player_tests PROPERTIES TIMEOUT 10)
    if(WIN32)
        # CPU only: the row's logic. "live" also samples this machine's GPU counters and video memory.
        add_executable(depthxr_performance_tests tests/performance_tests.cpp)
        target_link_libraries(depthxr_performance_tests PRIVATE depthxr_core dxgi)
        add_test(NAME depthxr_performance_tests COMMAND depthxr_performance_tests)
        set_tests_properties(depthxr_performance_tests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(depthxr_thread_census_tests tests/thread_census_tests.cpp)
    target_link_libraries(depthxr_thread_census_tests PRIVATE depthxr_core)
    add_test(NAME depthxr_thread_census_tests COMMAND depthxr_thread_census_tests)
    set_tests_properties(depthxr_thread_census_tests PROPERTIES TIMEOUT 10)

    add_executable(
        depthxr_action_set_attachment_tests
        tests/action_set_attachment_tests.cpp
    )
    target_include_directories(depthxr_action_set_attachment_tests PRIVATE include)
    add_test(NAME depthxr_action_set_attachment_tests COMMAND depthxr_action_set_attachment_tests)
    set_tests_properties(depthxr_action_set_attachment_tests PROPERTIES TIMEOUT 10)
endif()
