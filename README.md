> Current snapshot: Z016 String B / one-SMB read-only link candidate. See [implementation and validation plan](docs/z016/implementation_plan.md). Older closeout descriptions below are historical; use Z016_WORKTREE_SHA256SUMS.txt for this package.

author: @Mahad-Faisal
WORK IN PROGRESS R&D MAIN REPO IS DER26AMS

Current migration stage: **Z-015 audited private SPI6 transport plus post-review current-ADC/fan HAL hardening — host/source/SIL GREEN; base and IWDG target images compile/link GREEN; full target-contract rerun pending after a checker-only comment-filter correction**.
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

Pinned-Zephyr compatibility note: Zephyr v4.4.0 `include/zephyr/irq.h` has no public `k_irq_clear_pending()` API and the STM32F767 configuration does not provide `CONFIG_ARCH_HAS_IRQ_PENDING_OPS`. Board-specific SPI6/current-ADC/fan IRQ hardening therefore uses Zephyr `irq_disable()` plus CMSIS `NVIC_ClearPendingIRQ()`. Host fakes intentionally mirror that real v4.4 surface so this mismatch cannot be hidden again.

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

After the target-contract comment-filter correction, the final canonical host/SIL run passed **56/56 stages** in **88.090 s** with no skipped evidence; ThreadSanitizer was requested and performed. The Z-015 mutation suite rejects **47** unsafe changes. Both STM32F767 base and IWDG images have separately compiled/linked successfully; complete target contract completion and all physical validation remain open.

Z-015 design/evidence:

- `docs/migration/Z015_SPI_TRANSPORT_ORACLE.md`
- `docs/migration/Z015_STM32_SPI_DRIVER_AUDIT.md`
- `docs/migration/Z015_ADBMS_CALLER_INVENTORY.md`
- `docs/migration/Z015_IMPLEMENTATION_CLOSEOUT_2026-09-07.md`
- `docs/migration/Z015_POST_REVIEW_HAL_DRIVER_AUDIT_2026-09-07.md`
- `docs/migration/evidence/Z015_TARGET_CONTRACT_COMMENT_FILTER_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z015_TARGET_CONTRACT_COMMENT_FILTER_HOST_SIL_REPORT_2026-09-08.json`
- `docs/migration/Z015_TARGET_CONTRACT_COMMENT_FILTER_PACKAGE_CLOSEOUT_2026-09-08.md`
- historical pre-target-build evidence remains under the 2026-09-07 filenames

## Target contracts

For a completed target build:

```text
python scripts/check_all_contracts.py . <build-dir>
```

The previous hardened Z-014 snapshot passed real Zephyr 4.4 base and
IWDG-validation target builds. The Z-015 base and IWDG images have now both compiled and linked on the real
STM32F767 target configuration. The last observed gate failure was checker-only:
`check_current_adc_contract.py` matched `adc_context` in a provenance comment.
That checker is fixed in this package; target-green status still requires the
complete `check_all_contracts.py` run to finish on both existing build directories.
No physical-validation claim is made.
