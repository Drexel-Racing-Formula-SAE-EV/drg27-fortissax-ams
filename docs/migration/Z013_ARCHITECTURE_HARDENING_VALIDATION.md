# Z-013 architecture-hardening validation record

Date: 2026-09-07

Frozen behavior oracle: DER26 AMS v2.6.27 / FW 0.5.30.

This record covers host/source evidence for the architecture-hardening candidate
that follows the target-green pre-hardening Z-013 IMD image. It does **not**
claim that the changed CMake graph/custom bindings have passed the real
STM32F767 Zephyr target build yet.

## Mechanical portability gate

The real `lib/ams_core/CMakeLists.txt` was configured with ordinary host CMake,
compiled as 13 core translation units with no Zephyr/RTOS/HAL/platform include
paths, and executed through CTest.

Result:

```text
PASS: ams_core true null-platform build/test and compile-path audit (13 core TUs)
```

A negative control injected `#include <zephyr/kernel.h>` into a temporary copy
of a core translation unit. The same gate rejected the build, proving that the
check is not merely reporting a predeclared source property.

The repository also contains a native GitHub Actions job that runs this exact
gate on push and pull request.

## Host adapter / portable-core regression evidence

- BMS_OK production Zephyr adapter: 19 checks PASS; GCC `-fanalyzer` PASS;
  ASan/UBSan PASS.
- Current sensor: 76 checks PASS.
- Current fault: 22 checks PASS.
- Production current ADC adapter: 516,547 checks PASS.
- Current ADC low-timeout: 14 checks PASS.
- Current ADC ambiguous-completion: 6 checks PASS.
- Current path GCC `-fanalyzer`: PASS.
- Fan portable policy: 688 checks PASS.
- Production fan PWM adapter: 150,679 checks PASS.
- Fan GCC `-fanalyzer`: PASS.
- Fan ASan/UBSan: PASS.
- IMD portable core: 57 checks PASS.
- Production IMD capture adapter: 776,535 checks PASS.
- IMD GCC `-fanalyzer`: PASS.
- IMD ASan/UBSan: PASS.
- Clang static analyzer: BMS_OK/current ADC/fan PWM/IMD adapters PASS.
- Measurement store: 8/8 PASS.
- Current-window regression: 9/9 PASS.
- Estimator core: 89 checks PASS.
- Fuse observer fast oracle suite: 5 tests PASS, including 50k randomized
  production/reference comparison.
- SoH: 236 checks PASS.
- FreeRTOS v2.6.27 runtime/safety source parity contract: PASS.

No production `.c`/`.h` file was changed by the null-platform/fake-DT/schema
closeout relative to the uploaded architecture-hardening WIP. The production
changes that still require target proof are the architecture WIP itself:
platform interfaces/libraries, typed custom Devicetree consumers, board-owned
fail-low source, and capability assertions.

## Deliberately not rerun

The expensive Z-010 SoP metamorphic campaign was not redundantly rerun because
this architecture hardening does not modify any SoP/SoH/fuse production source.
Existing target/differential evidence remains the regression oracle for those
algorithms; the target contract suite must still pass after the architecture
changes.


## First target configure result and correction

The first real STM32F767 closeout configure reached board/DTS/Kconfig generation
successfully, then stopped in CMake before compilation.  The failure was in the
new architecture build composition, not in AMS production C source:

- Zephyr had already created `ams_board_safety` from the selected DER26 board,
  while `app/CMakeLists.txt` attempted to add the board directory a second time;
- `drivers/ams` was being added from application CMake mode, causing Zephyr to
  warn that `ams_platform` would not be treated as a Zephyr library.

The candidate was corrected so normal platform drivers are loaded through the
repository's Zephyr module integration (`zephyr/module.yml` ->
`zephyr/CMakeLists.txt`) and board safety is left exclusively to Zephyr board
integration.  No production `.c`/`.h` behavior was changed by this correction.
A fresh target build is still required before the architecture-hardened stage is
called target-green.

## Required target closeout

From the user's Zephyr workspace:

```powershell
Set-Location 'C:\DER_AMS\git\revert\DER27-AMS-zephyr'

west build `
    -p always `
    -b der26_ams `
    app `
    -d build\z013_arch_hardened

py scripts\check_all_contracts.py . build\z013_arch_hardened
```

The unified checker now also runs the null-platform gate. A complete local gate
therefore requires a host-native C compiler/CMake environment in addition to
the Zephyr ARM toolchain. If that native compiler is not installed on Windows,
the GitHub native CI result is the required portability evidence; do not label
the stage fully green until both the target suite and native CI are green.

Until target closeout succeeds:

- BMS_OK assertion authority remains disabled;
- balancing authority remains disabled;
- CAN1 remains disabled;
- SPI6 remains disabled;
- IWDG remains disabled until Z-014;
- current actor remains non-evidence;
- fan/IMD physical-validation claims remain false.
