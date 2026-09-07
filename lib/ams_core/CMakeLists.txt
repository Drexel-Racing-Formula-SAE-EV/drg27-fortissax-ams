add_library(ams_core STATIC
    common/ams_core_time.c
    common/ams_core_contract.c
    measurement/ams_measurement.c
    measurement/ams_current_window.c
    estimator/ams_estimator_lut.c
    estimator/ams_soc_ekf.c
    soh/ams_soh.c
    sop/ams_sop.c
    sop/ams_fuse_observer.c
)

target_include_directories(ams_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)