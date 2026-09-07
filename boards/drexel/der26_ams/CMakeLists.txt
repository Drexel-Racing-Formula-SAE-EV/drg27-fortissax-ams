# Board-only emergency safety primitive. This is deliberately a separate
# Zephyr library from normal platform adapters so direct STM32 register access
# cannot spread into ordinary driver or application code.
#
# Zephyr loads this CMakeLists automatically for board der26_ams.  The
# application must never add this board directory manually.

get_filename_component(
    DRG27_AMS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../.."
    ABSOLUTE
)

zephyr_library_named(ams_board_safety)

zephyr_library_sources(
    ams_fail_low_stm32.c
)

zephyr_library_include_directories(
    ${DRG27_AMS_ROOT}/include
)
