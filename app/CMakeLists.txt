cmake_minimum_required(VERSION 3.20.0)

get_filename_component(DRG27_AMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
list(APPEND BOARD_ROOT "${DRG27_AMS_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drg27_fortissax_ams)

target_sources(app PRIVATE
    src/main.c
    src/ams_safety.c
    src/ams_threads.c
    ${DRG27_AMS_ROOT}/drivers/ams/current_adc_zephyr.c
)

target_include_directories(app PRIVATE
    ${DRG27_AMS_ROOT}/drivers/ams
)

# ---------------------------------------------------------------------------
# Z-006 portable AMS core
# ---------------------------------------------------------------------------

get_filename_component(
    AMS_CORE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/ams_core"
    ABSOLUTE
)

add_subdirectory(
    "${AMS_CORE_DIR}"
    "${CMAKE_BINARY_DIR}/ams_core"
)

target_link_libraries(app PRIVATE ams_core)