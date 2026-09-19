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

# Shared Windows API, warnings and runtime linkage for project-owned code.
function(witness_target_defaults target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endfunction()

function(witness_unicode_entry target)
    if(MINGW)
        target_link_options(${target} PRIVATE -municode)
    endif()
endfunction()

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_subdirectory(src)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
