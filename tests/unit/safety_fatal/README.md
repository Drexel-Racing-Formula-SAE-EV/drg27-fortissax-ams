# Zephyr fatal/fail-low composition host test

Host-compiles the production `app/src/ams_safety.c` against minimal Zephyr
stubs and proves the application fatal override orders the direct BMS_OK
fail-low action before fatal halt, latches panic before halt, and repeats the
fail-low action on subsequent fatal entry. The normal BMS_OK GPIO adapter is
tested separately in `tests/unit/bms_ok`.
