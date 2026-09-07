# Z-013 architecture hardening — target CMake composition fix

Date: 2026-09-07

## Failure observed on the first real target configure

The STM32F767 Zephyr 4.4 configure successfully reached board discovery,
Devicetree generation and Kconfig generation, then failed in CMake with two
related architecture-WIP composition problems:

1. `drivers/ams/CMakeLists.txt` was manually added from `app/CMakeLists.txt`
   after `find_package(Zephyr)`. Zephyr correctly warned that
   `zephyr_library_named(ams_platform)` was being called in application CMake
   mode and therefore would not be treated as a normal Zephyr kernel library.
2. `boards/drexel/der26_ams/CMakeLists.txt` was manually added from the app even
   though Zephyr had already processed the selected board. The second inclusion
   attempted to create `ams_board_safety` again and CMake failed on the
   duplicate target.

No C source compile had started when this happened, so the failure did not
invalidate the prior algorithm/adapter host evidence. It did show that the new
architecture build ownership had not yet been target-proven.

## Correct ownership

```text
manifest repository module discovery
  zephyr/module.yml
      |
      v
  zephyr/CMakeLists.txt            (Zephyr kernel CMake mode)
      |
      v
  drivers/ams/CMakeLists.txt
      |
      v
  ams_platform

selected board discovery
  boards/drexel/der26_ams/CMakeLists.txt
      |
      v
  ams_board_safety                 (exactly once)

application CMake
  app sources
  portable lib/ams_core
  link app/platform -> ams_core
  NO manual add_subdirectory(drivers/ams)
  NO manual add_subdirectory(board directory)
```

`drivers/ams/CMakeLists.txt` and the board CMake file also derive the repository
root from their own `CMAKE_CURRENT_LIST_DIR`, removing reliance on an
application-scope path variable.

The architecture contract now rejects regression to the previous manual
composition pattern.

## Validation status

Completed after the fix:

- Python contract-script syntax compile: PASS;
- true null-platform `ams_core` build/test: 13 translation units PASS;
- FreeRTOS v2.6.27 runtime/safety source parity: PASS;
- changed-file whitespace audit: PASS.

Still required:

```powershell
west build `
    -p always `
    -b der26_ams `
    app `
    -d build\z013_arch_hardened

py scripts\check_all_contracts.py . build\z013_arch_hardened
```

Do not call the architecture-hardened stage target-green until both commands
complete successfully.
