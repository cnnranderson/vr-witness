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
configure_file(resources/version.h.in "${PROJECT_BINARY_DIR}/generated/version.h" @ONLY)

add_library(witness-launcher-core STATIC
    backend.cpp backend_commands.cpp
    services/game_readiness.cpp services/loader_client.cpp services/platform.cpp
    services/settings.cpp services/steam_discovery.cpp)
witness_target_defaults(witness-launcher-core)
target_link_libraries(witness-launcher-core PUBLIC shell32 advapi32 bcrypt user32)

set(WITNESS_LAUNCHER_UI_SOURCES)
foreach(source main.cpp ui/window.cpp ui/window_host.cpp ui/window_layout.cpp ui/status_page.cpp ui/settings_page.cpp resources/launcher.rc)
    list(APPEND WITNESS_LAUNCHER_UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
endforeach()
set(WITNESS_LAUNCHER_UI_SOURCES "${WITNESS_LAUNCHER_UI_SOURCES}" PARENT_SCOPE)

function(witness_launcher_target target)
    witness_target_defaults(${target})
    witness_unicode_entry(${target})
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src/launcher/resources" "${PROJECT_SOURCE_DIR}/assets" "${PROJECT_BINARY_DIR}/generated")
    target_link_libraries(${target} PRIVATE witness-launcher-core comctl32 comdlg32 gdi32)
    set_property(SOURCE "${PROJECT_SOURCE_DIR}/src/launcher/resources/launcher.rc"
        APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/assets/launcher.ico")
endfunction()

add_executable(WitnessVR WIN32 ${WITNESS_LAUNCHER_UI_SOURCES})
witness_launcher_target(WitnessVR)
