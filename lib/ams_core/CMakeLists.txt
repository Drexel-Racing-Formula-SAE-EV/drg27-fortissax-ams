add_library(ams_core STATIC
    common/ams_core_time.c
    common/ams_core_contract.c
    measurement/ams_measurement.c
    measurement/ams_current_window.c
)

target_include_directories(ams_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)