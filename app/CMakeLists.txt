cmake_minimum_required(VERSION 3.20.0)

get_filename_component(DRG27_AMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND BOARD_ROOT "${DRG27_AMS_ROOT}")
list(APPEND DTS_ROOT "${DRG27_AMS_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drg27_fortissax_ams)

target_sources(app PRIVATE
    src/main.c
    src/ams_safety.c
    src/ams_threads.c
)

target_include_directories(app PRIVATE
    "${DRG27_AMS_ROOT}/include"
)

# ---------------------------------------------------------------------------
# Z-006 portable AMS core
# ---------------------------------------------------------------------------
#
# Keep the portable core as ordinary CMake.  Its own CMakeLists.txt contains
# no Zephyr helpers and is reused verbatim by the true null-platform host gate.
# Platform integration points *toward* this core, never the reverse.
get_filename_component(
    AMS_CORE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/ams_core"
    ABSOLUTE
)

add_subdirectory(
    "${AMS_CORE_DIR}"
    "${CMAKE_BINARY_DIR}/ams_core"
)

# drivers/ams is intentionally NOT added here.  This repository is a Zephyr
# module and zephyr/CMakeLists.txt creates ams_platform while Zephyr is still
# in kernel CMake mode.  Calling zephyr_library_named() from this application
# CMakeLists would happen too late (application mode) and is explicitly
# rejected by the architecture contract.
if(NOT TARGET ams_platform)
    message(FATAL_ERROR
        "ams_platform Zephyr module library was not created; "
        "check zephyr/module.yml and zephyr/CMakeLists.txt")
endif()

# The DER26 board CMakeLists is discovered and processed by Zephyr as part of
# board handling.  Never add the board directory manually from the app: doing
# so instantiates ams_board_safety twice and violates board-source ownership.
if(NOT TARGET ams_board_safety)
    message(FATAL_ERROR
        "ams_board_safety was not created by the DER26 board CMake integration")
endif()

# Platform adapters may consume portable AMS types/functions.  The portable
# core itself never receives a dependency back on the platform or Zephyr.
target_link_libraries(ams_platform PRIVATE ams_core)
target_link_libraries(app PRIVATE ams_core)
