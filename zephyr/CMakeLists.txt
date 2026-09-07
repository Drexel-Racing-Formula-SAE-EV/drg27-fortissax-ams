# Zephyr module integration for DRG27 AMS.
#
# This file is loaded by Zephyr while the kernel build graph is still being
# assembled.  Ordinary platform adapters belong here so zephyr_library_named()
# is used in kernel mode, not from the application CMakeLists after
# find_package(Zephyr) has completed.

get_filename_component(
    DRG27_AMS_MODULE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

add_subdirectory(
    "${DRG27_AMS_MODULE_ROOT}/drivers/ams"
    "${CMAKE_CURRENT_BINARY_DIR}/ams_platform"
)
