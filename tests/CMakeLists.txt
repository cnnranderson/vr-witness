add_executable(witness-probe-test-host diagnostics/host.cpp)
add_executable(witness-render-test-host render/host.cpp render/targets.cpp)
add_executable(witness-input-test-host input/host.cpp input/targets.cpp input/height.cpp)
foreach(target witness-probe-test-host witness-render-test-host witness-input-test-host)
    witness_target_defaults(${target})
    witness_unicode_entry(${target})
endforeach()

add_executable(witness-input-math-test input/math.cpp)
witness_target_defaults(witness-input-math-test)
add_test(NAME controller_math COMMAND witness-input-math-test)
add_test(NAME resting_height_hook COMMAND witness-input-test-host --height)
set_tests_properties(resting_height_hook PROPERTIES TIMEOUT 20)

add_executable(witness-launcher-test launcher/backend.cpp)
witness_target_defaults(witness-launcher-test)
target_link_libraries(witness-launcher-test PRIVATE witness-launcher-core)
add_test(NAME launcher_backend COMMAND witness-launcher-test)
set_tests_properties(launcher_backend PROPERTIES TIMEOUT 15)

# Built explicitly for desktop UI review; never shipped in the portable package.
add_executable(witness-launcher-ui-test WIN32 EXCLUDE_FROM_ALL
    ${WITNESS_LAUNCHER_UI_SOURCES} launcher/ui_smoke.cpp)
witness_launcher_target(witness-launcher-ui-test)
target_compile_definitions(witness-launcher-ui-test PRIVATE WITNESS_UI_TEST)

function(witness_process_test name script timeout)
    add_test(NAME ${name}
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/${script}" -BinaryDir "$<TARGET_FILE_DIR:witness-probe-loader>"
        -OutputDir "${PROJECT_BINARY_DIR}/test-logs")
    if("$ENV{GITHUB_ACTIONS}" STREQUAL "true")
        set(timeout 120)
    endif()
    set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
endfunction()

witness_process_test(probe_injection_smoke smoke.ps1 40)
witness_process_test(render_injection_smoke render_smoke.ps1 40)
witness_process_test(cursor_session_smoke session_smoke.ps1 70)
witness_process_test(controller_injection_smoke input_smoke.ps1 40)
witness_process_test(controller_session_smoke input_session_smoke.ps1 70)
witness_process_test(snap_turn_smoke snap_smoke.ps1 45)
witness_process_test(stick_cursor_smoke stick_smoke.ps1 25)
