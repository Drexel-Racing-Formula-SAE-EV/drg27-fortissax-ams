cmake_minimum_required(VERSION 3.20)

project(ams_core_null_platform LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(AMS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(AMS_CORE_DIR "${AMS_REPO_ROOT}/lib/ams_core")

# This is deliberately ordinary CMake. There is no find_package(Zephyr),
# RTOS shim, board root, HAL include tree, or platform adapter dependency.
add_subdirectory("${AMS_CORE_DIR}" "${CMAKE_BINARY_DIR}/ams_core")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ams_core PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wpedantic
    )
endif()

add_executable(ams_core_null_platform_smoke
    null_platform_smoke.c
)

target_link_libraries(ams_core_null_platform_smoke PRIVATE
    ams_core
)

# ams_core intentionally uses the ISO C math library. Zephyr supplies this in
# its target environment; host-native Unix toolchains need the ordinary m lib.
if(UNIX)
    target_link_libraries(ams_core_null_platform_smoke PRIVATE m)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ams_core_null_platform_smoke PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wpedantic
    )
endif()

enable_testing()
add_test(NAME ams_core_null_platform_smoke
         COMMAND ams_core_null_platform_smoke)
