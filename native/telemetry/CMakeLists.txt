# Can be built independently for parser development, or included by native/.
cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
 project(OpenGridTelemetry LANGUAGES CXX)
 set(CMAKE_CXX_STANDARD 17)
 set(CMAKE_CXX_STANDARD_REQUIRED ON)
 enable_testing()
endif()
add_library(opengrid_telemetry STATIC telemetry.cpp)
target_include_directories(opengrid_telemetry PUBLIC .)
target_compile_definitions(opengrid_telemetry PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
add_executable(opengrid-telemetry-tests tests/parser_tests.cpp)
target_link_libraries(opengrid-telemetry-tests PRIVATE opengrid_telemetry)
add_test(NAME native-telemetry-parser COMMAND opengrid-telemetry-tests)
if(WIN32)
 add_executable(OpenGridXR-Telemetry main.cpp)
 target_link_libraries(OpenGridXR-Telemetry PRIVATE opengrid_telemetry)
 add_executable(opengrid-telemetry-fixture tests/fixture_writer.cpp)
 target_link_libraries(opengrid-telemetry-fixture PRIVATE opengrid_telemetry)
 set_target_properties(OpenGridXR-Telemetry opengrid-telemetry-fixture opengrid-telemetry-tests
   PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
 add_test(NAME native-telemetry-io COMMAND powershell -NoProfile -ExecutionPolicy Bypass
   -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/helper_smoke.ps1"
   -Helper $<TARGET_FILE:OpenGridXR-Telemetry> -Fixture $<TARGET_FILE:opengrid-telemetry-fixture>)
 add_test(NAME native-telemetry-io-delayed-reader COMMAND powershell -NoProfile -ExecutionPolicy Bypass
   -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/helper_smoke.ps1"
   -Helper $<TARGET_FILE:OpenGridXR-Telemetry> -Fixture $<TARGET_FILE:opengrid-telemetry-fixture>
   -ReaderDelayMs 1200)
 set_tests_properties(native-telemetry-io native-telemetry-io-delayed-reader PROPERTIES TIMEOUT 40)
endif()
