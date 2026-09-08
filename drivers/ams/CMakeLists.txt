# AMS-specific Zephyr platform boundary. Application/task code consumes only
# the generic headers under include/ams_platform; STM32/Zephyr driver details
# remain compiled in this dedicated Zephyr library.
#
# This file is reached through zephyr/CMakeLists.txt while Zephyr is in kernel
# CMake mode.  Do not add this directory from app/CMakeLists.txt.

get_filename_component(
    DRG27_AMS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

zephyr_library_named(ams_platform)

zephyr_library_sources(
    bms_ok_zephyr.c
    current_adc_stm32.c
    fan_pwm_zephyr.c
    imd_capture_zephyr.c
    watchdog_zephyr.c
    adbms_spi_engine.c
    adbms_spi_stm32.c
)

zephyr_library_include_directories(
    ${DRG27_AMS_ROOT}/include
    ${DRG27_AMS_ROOT}/lib/ams_core/include
)

zephyr_library_sources_ifdef(CONFIG_AMS_Z016_LINK_PROBE
    adbms_time_zephyr.c adbms_link_probe.c)
