author: @Mahad-Faisal
WORK IN PROGRESS R&D MAIN REPO IS DER26AMS

Current migration stage: **Z-015 audited private SPI6 transport plus post-review current-ADC/fan HAL hardening — host/source/SIL GREEN, target rebuild pending**.
This image remains compile-time no-authority: BMS_OK assertion and balancing are
absent/disabled. ADBMS actor/safety evidence and physical SPI validation remain
false. No ADBMS6822 wake/isoSPI protocol, ADBMS6830/APM protocol, acquisition,
balancing, or Z-016 work is included.

## Migration architecture invariant

`lib/ams_core` is host-native portable C: no Zephyr, FreeRTOS/CMSIS, STM32 HAL,
board headers, or `ams_platform` dependency. Normal hardware access belongs in
`drivers/ams` behind narrow interfaces and typed Devicetree bindings.

Three narrowly approved hardware-specific seams exist because the generic stack
cannot provide the required safety behavior: the board-owned PE0 BMS_OK
emergency fail-low primitive, the Z-015 private STM32F767 SPI6 polling backend,
and the post-review private STM32F767 ADC1/ADC2 current-sense polling backend.
The SPI raw-transfer API remains private with zero runtime callers at Z-015.
ADC1/ADC2/ADC3 generic Zephyr devices remain disabled so current timeout recovery
does not inherit uncertain `adc_context`/ISR ownership. `CONFIG_LTO=n` is frozen
so the final-ELF SPI caller proof cannot be defeated by cross-TU inlining.

The application runtime remains static/no-heap and does not use the Zephyr
system workqueue for current safety work.

## Canonical Z-015 host/SIL gate

On a host with GNU make, GCC and Clang:

```text
python scripts/run_z015_host_validation.py . --require-clang --tsan
```

This reruns the current Z-001..Z-014 source/host regressions plus the Z-015
bounded SPI engine, the post-review private current-ADC/fan HAL hardening,
sanitizers, static analyzers, and mutation controls. It intentionally does not
target-build/flash, perform physical SPI/ADC/PWM/IWDG validation, or advance to
Z-016.

The final post-review canonical host/SIL run passed **54/54 stages** in **85.990 s** with no skipped evidence; ThreadSanitizer was requested and performed. Target and physical validation were not performed.

Z-015 design/evidence:

- `docs/migration/Z015_SPI_TRANSPORT_ORACLE.md`
- `docs/migration/Z015_STM32_SPI_DRIVER_AUDIT.md`
- `docs/migration/Z015_ADBMS_CALLER_INVENTORY.md`
- `docs/migration/Z015_IMPLEMENTATION_CLOSEOUT_2026-09-07.md`
- `docs/migration/Z015_POST_REVIEW_HAL_DRIVER_AUDIT_2026-09-07.md`
- `docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_CANONICAL_FINAL_2026-09-07.log`
- `docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_REPORT_FINAL_2026-09-07.json`

## Target contracts

For a completed target build:

```text
python scripts/check_all_contracts.py . <build-dir>
```

The previous hardened Z-014 snapshot passed real Zephyr 4.4 base and
IWDG-validation target builds. Z-015 changes Devicetree/Kconfig and adds the
private SPI6 target backend, so target-green status must be re-earned with a
fresh build of this exact snapshot. No physical-validation claim is made.
