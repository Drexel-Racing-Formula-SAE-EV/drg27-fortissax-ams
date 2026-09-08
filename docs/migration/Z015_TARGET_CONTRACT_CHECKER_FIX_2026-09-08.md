# Z-015 target architecture-contract checker correction — 2026-09-08

## Trigger

Fresh Zephyr 4.4.0 STM32F767 target builds completed successfully for both the
base and IWDG configurations, but `scripts/check_all_contracts.py` stopped in
`check_architecture_contract.py` with:

`legacy HAL/RTOS dependency in drivers/ams/current_adc_stm32.c: HAL_ADC_Start(`

The production current ADC backend does not call `HAL_ADC_Start()`. The token is
present only in comments documenting exact v2.6.27 oracle behavior.

## Root cause

The target architecture checker applied its legacy HAL/RTOS call regex directly
to raw C source including comments. It therefore classified oracle-provenance
comments as executable dependencies.

A second stale rule was found while fixing the first: the checker still required
`ams_fail_low_stm32.c` to be the *only* direct STM32/CMSIS owner even though the
audited Z-015 architecture intentionally introduced three narrowly scoped
platform seams:

- `drivers/ams/adbms_spi_stm32.c` — private bounded SPI6 LL backend;
- `drivers/ams/current_adc_stm32.c` — private bounded ADC1/ADC2 LL backend;
- `drivers/ams/fan_pwm_zephyr.c` — output-only TIM3/TIM4/TIM5 NVIC pending clear.

Without correcting this stale rule, the target checker would have failed on the
next stage after the comment false positive was removed.

## Correction

`check_architecture_contract.py` now strips C/C++ comments before code-level
dependency scans while preserving strings, character literals and newlines.
Real HAL calls/types/includes remain forbidden.

Direct STM32/CMSIS ownership is now an exact-set contract containing only:

1. `boards/drexel/der26_ams/ams_fail_low_stm32.c`;
2. `drivers/ams/adbms_spi_stm32.c`;
3. `drivers/ams/current_adc_stm32.c`;
4. `drivers/ams/fan_pwm_zephyr.c`.

Any additional direct owner remains a contract failure.

A new host regression test, `scripts/check_architecture_contract_host_selftest.py`,
executes the real target architecture checker with a generated-DTS fixture and
proves all three conditions:

- HAL function names in oracle comments are ignored;
- a real injected `HAL_ADC_Start()` call is rejected;
- a new application-level `soc.h`/NVIC direct owner is rejected.

The canonical Z-015 host runner now includes this checker regression stage.

## Target build status

The C/config/DTS production inputs are unchanged from the prior NVIC-compatible
package. The user-side target compiler successfully linked both images before the
checker failure:

- base: FLASH 162,876 B; RAM 45,824 B;
- IWDG: FLASH 162,932 B; RAM 45,824 B.

Therefore this correction changes checker/evidence infrastructure only; it does
not alter either target image. The existing target build directories may be
reused to rerun `check_all_contracts.py` after applying this package. A fresh
build is still acceptable if a fully pristine closeout is preferred.

## Validation

Post-correction targeted evidence:

- Python checker syntax: PASS;
- architecture checker host regression: PASS;
- Z-014 source hygiene: PASS;
- Z-015 source hygiene: PASS;
- Z-014 mutation suite: 18 unsafe mutations rejected;
- Z-015 mutation suite: 47 unsafe mutations rejected;
- null-platform core build/test: PASS.

The target checker itself still requires the real user-side Zephyr build
artifacts for final target-closeout execution.
