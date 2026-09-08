# Z-015 target ADC Kconfig checker correction

Date: 2026-09-08

## State and scope

FreeRTOS v2.6.27 / firmware 0.5.30 remains the frozen behavioral oracle.
Z-015 retains private bounded SPI6 and recoverable ADC1/ADC2 ownership,
ADC3 common-reset exclusion, fan TIM3/4/5 IRQ quiescence, and TIM2 IMD capture.
BMS_OK/balancing authority and ADBMS runtime transfer callers remain absent.
Z-016 and physical validation remain open.

The user's base and IWDG images previously compiled and linked. Their latest
contract runs passed the first ten checks and stopped at the ADC configuration
check. Remaining checks have not been shown to pass on these target images.

## Defect and correction

The checker required an explicit disabled record for CONFIG_ADC_ASYNC and
CONFIG_ADC_STM32_DMA. Generated Kconfig may omit dependent symbols when
CONFIG_ADC is disabled. A synthetic configuration with omitted children
reproduces the exact reported failure in the original shipped checker.
The user's actual generated .config files were not supplied here.

The correction permits absent ADC async/DMA children only after requiring
explicitly disabled CONFIG_ADC. Exact configuration records are parsed;
any non-n assignment is rejected even alongside a contradictory disabled
comment or assignment. Required private-backend/LL symbols, disabled generic
ADC nodes, and linked-symbol exclusions are retained.

Only three existing Python files changed: the ADC checker, its host regression,
and the canonical runner's regression-stage label. No production C, headers,
DTS, Kconfig, CMake or linker inputs changed. Existing target images can be
reused if they were built from the supplied production snapshot.

## Validation performed for this correction

- Original uploaded package SHA-256 manifest verified.
- Original checker reproduced the reported omitted-dependent-symbol failure.
- Corrected full ADC checker passed 18 synthetic configuration cases, including
  omitted/explicitly disabled children and rejection of enabled, invalid,
  contradictory settings and a missing parent record.
- Existing ADC comment-filter regression passed.
- Target architecture checker host regression passed.
- Z-015 source/architecture hygiene contract passed.
- All Python checker scripts parsed successfully.
- Production inputs compared byte-for-byte against the uploaded archive.
- Repackaged archive CRC and every SHA-256 manifest entry verified.

Synthetic DTS/map/config fixtures are checker tests, not target-build evidence.
The complete canonical host/SIL campaign was not rerun for this Python-only
correction. Earlier canonical reports are retained as historical evidence, not
relabeled as a new run. No local target build or hardware test was performed.

## Required target continuation

Retain build/z015_nvic_base and build/z015_nvic_iwdg, apply the corrected
repository scripts, then run from the repository root:

```powershell
py scripts\check_all_contracts.py . build\z015_nvic_base
py scripts\check_all_contracts.py . build\z015_nvic_iwdg
```

Complete target-green status remains pending both full-suite results.
