author: @Mahad-Faisal
WORK IN PROGRESS R&D MAIN REPO IS DER26AMS

Current migration stage: **Z-014 watchdog/IWDG source/SIL hardened candidate**.
This image remains compile-time no-authority: BMS_OK assertion and balancing are
absent/disabled. Physical watchdog, IMD and fan validation claims remain false.
No Z-015 work is included.

## Migration architecture invariant

`lib/ams_core` is host-native portable C: no Zephyr, FreeRTOS/CMSIS, STM32 HAL,
board headers, or `ams_platform` dependency. Normal hardware access belongs in
`drivers/ams` behind narrow `include/ams_platform` interfaces and typed
Devicetree bindings. The sole direct-register exception is the board-owned PE0
BMS_OK emergency fail-low primitive, which exists specifically for pre-kernel
and fatal-path independence.

The application runtime remains static/no-heap and does not use the Zephyr
system workqueue for current safety work.

## Canonical Z-014 host/SIL gate

On a host with GNU make, GCC and Clang:

```text
python scripts/run_z014_host_validation.py . --require-clang --tsan
```

This runs source/oracle contracts, true null-platform compilation, current-scope
unit and integrated SIL, ASan/UBSan, watchdog TSan, GCC `-fanalyzer`, Clang
analysis and mutation/negative-control tests. It intentionally does not build or
flash a target and does not run later-stage ADBMS/CAN/authority tests.

Deep review and evidence:

- `docs/migration/Z014_DEEP_SAFETY_ARCHITECTURE_SIL_REVIEW_2026-09-07.md`
- `docs/migration/Z014_HOST_VALIDATION_EVIDENCE_2026-09-07.json`

## Target contracts

For a completed target build:

```text
python scripts/check_all_contracts.py . <build-dir>
```

The pre-hardening Z-014 tree passed real Zephyr 4.4 base and IWDG-validation
target builds. Because this hardened snapshot changes production watchdog/runtime
source, it requires a fresh target rebuild before target-green status can be
re-established. No physical-validation claim is made by this repository.
