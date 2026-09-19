cmake_minimum_required(VERSION 3.25)
project(witness_vr LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Witness VR requires a 64-bit Windows toolchain.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
endforeach()

add_library(witness-probe SHARED src/probe.cpp)
set_target_properties(witness-probe PROPERTIES PREFIX "")
add_executable(witness-probe-loader src/loader.cpp)
add_executable(witness-probe-test-host tests/probe_host.cpp)
add_library(minhook STATIC third_party/minhook/src/buffer.c third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)
add_library(witness-render-probe SHARED src/render_probe.cpp)
set_target_properties(witness-render-probe PROPERTIES PREFIX "")
target_link_libraries(witness-render-probe PRIVATE minhook bcrypt user32)
add_executable(witness-render-test-host tests/render_host.cpp tests/render_targets.cpp)
add_library(witness-input-probe SHARED src/input_probe.cpp)
set_target_properties(witness-input-probe PROPERTIES PREFIX "")
target_link_libraries(witness-input-probe PRIVATE minhook bcrypt user32)
add_executable(witness-input-test-host tests/input_host.cpp tests/input_targets.cpp)
add_executable(witness-input-math-test tests/input_math.cpp)

foreach(target witness-probe witness-probe-loader witness-probe-test-host witness-render-probe witness-render-test-host witness-input-probe witness-input-test-host witness-input-math-test)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endforeach()
if(MINGW)
    target_link_options(witness-probe-loader PRIVATE -municode)
    target_link_options(witness-probe-test-host PRIVATE -municode)
    target_link_options(witness-render-test-host PRIVATE -municode)
    target_link_options(witness-input-test-host PRIVATE -municode)
endif()

set(WITNESS_RELEASE_VERSION "0.1.0-dev" CACHE STRING "Launcher release version")
if(NOT WITNESS_RELEASE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[A-Za-z0-9][A-Za-z0-9.-]*)?$")
    message(FATAL_ERROR "Use a version such as 0.1.0 or 0.1.0-preview.2")
endif()
set(WITNESS_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(WITNESS_VERSION_MINOR "${CMAKE_MATCH_2}")
set(WITNESS_VERSION_PATCH "${CMAKE_MATCH_3}")
foreach(part MAJOR MINOR PATCH)
    if(WITNESS_VERSION_${part} GREATER 65535)
        message(FATAL_ERROR "Windows version components cannot exceed 65535")
    endif()
endforeach()
configure_file(src/launcher/version.h.in generated/version.h @ONLY)

add_executable(WitnessVR WIN32 src/launcher/main.cpp src/launcher/backend.cpp src/launcher/launcher.rc)
target_include_directories(WitnessVR PRIVATE src src/launcher assets "${CMAKE_BINARY_DIR}/generated")
set_source_files_properties(src/launcher/launcher.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/launcher.ico")
target_compile_definitions(WitnessVR PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(WitnessVR PRIVATE comctl32 comdlg32 shell32 advapi32 bcrypt gdi32 user32)
if(MSVC)
    target_compile_options(WitnessVR PRIVATE /W4 /WX /permissive- /EHsc)
    set_property(TARGET WitnessVR PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(WitnessVR PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(WitnessVR PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

enable_testing()
add_test(NAME probe_injection_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(probe_injection_smoke PROPERTIES TIMEOUT 40)
add_test(NAME render_injection_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/render_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(render_injection_smoke PROPERTIES TIMEOUT 40)
add_test(NAME cursor_session_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/session_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(cursor_session_smoke PROPERTIES TIMEOUT 70)

add_test(NAME controller_math COMMAND witness-input-math-test)
add_test(NAME controller_injection_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/input_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(controller_injection_smoke PROPERTIES TIMEOUT 40)

add_test(NAME controller_session_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/input_session_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(controller_session_smoke PROPERTIES TIMEOUT 70)

add_test(NAME snap_turn_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/snap_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(snap_turn_smoke PROPERTIES TIMEOUT 45)

add_test(NAME stick_cursor_smoke
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "${CMAKE_SOURCE_DIR}/tests/stick_smoke.ps1" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
    -OutputDir "${CMAKE_BINARY_DIR}/test-logs")
set_tests_properties(stick_cursor_smoke PROPERTIES TIMEOUT 25)

add_executable(witness-launcher-test tests/launcher_backend.cpp src/launcher/backend.cpp)
target_include_directories(witness-launcher-test PRIVATE src)
target_compile_definitions(witness-launcher-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(witness-launcher-test PRIVATE shell32 advapi32 bcrypt user32)
if(MSVC)
    target_compile_options(witness-launcher-test PRIVATE /W4 /WX /permissive- /EHsc)
    set_property(TARGET witness-launcher-test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(witness-launcher-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_options(witness-launcher-test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_test(NAME launcher_backend COMMAND witness-launcher-test)
set_tests_properties(launcher_backend PROPERTIES TIMEOUT 15)
