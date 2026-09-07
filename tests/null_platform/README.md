# AMS core null-platform gate

This project is the mechanical portability test for `lib/ams_core`. It builds
the **real** `lib/ams_core/CMakeLists.txt` using ordinary host CMake and runs the
core contract test without Zephyr, FreeRTOS, CMSIS, STM32 HAL, board files, or
`include/ams_platform` on the compiler search path.

Run from the repository root:

```text
python3 scripts/check_null_platform_core.py .
```

The checker also audits `compile_commands.json` so a future accidental include
path or target define cannot make the build pass by leaking the Zephyr/HAL
workspace into the supposedly portable target. This is intentionally stronger
than a source grep: if `ams_core` needs a kernel mutex, HAL type, DeviceTree
macro, or board include to compile, the architecture gate fails.

The gate validates portability/isolation. It does not replace the exact
v2.6.27 differential, directed, sanitizer, or target tests for the individual
algorithms.
