# Binary/export names are part of the loader protocol and package contract.
add_library(witness-probe SHARED diagnostics/module_probe.cpp)
add_library(witness-render-probe SHARED render/module.cpp)
add_library(witness-input-probe SHARED input/module.cpp)
foreach(target witness-probe witness-render-probe witness-input-probe)
    witness_target_defaults(${target})
    set_target_properties(${target} PROPERTIES PREFIX "")
endforeach()
target_link_libraries(witness-render-probe PRIVATE minhook bcrypt user32)
target_link_libraries(witness-input-probe PRIVATE minhook bcrypt user32)

add_executable(witness-probe-loader loader/main.cpp)
witness_target_defaults(witness-probe-loader)
witness_unicode_entry(witness-probe-loader)

add_subdirectory(launcher)
set(WITNESS_LAUNCHER_UI_SOURCES "${WITNESS_LAUNCHER_UI_SOURCES}" PARENT_SCOPE)
