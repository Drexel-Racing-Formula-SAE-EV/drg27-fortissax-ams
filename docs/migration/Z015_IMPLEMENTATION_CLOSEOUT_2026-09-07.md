# Z-015 Private SPI6 Transport + HAL Hardening — Software Closeout

Date: 2026-09-07

Status: **host/source/SIL GREEN after post-review HAL hardening; STM32F767 target rebuild required before target-green claim.**

This closeout is limited to Z-015. It does not perform physical SPI/ADC/PWM/IWDG tests and does not begin Z-016 ADBMS6822/isoSPI protocol work.

## Frozen boundary

The behavioral reference remains DER26 AMS v2.6.27 / FW 0.5.30. Z-015 adds only the ADBMS SPI6 transport substrate and closes platform/HAL findings discovered while reviewing the already-migrated current-ADC and fan-PWM paths. BMS_OK assertion authority and balancing authority remain disabled. ADBMS actor liveness, ADBMS safety evidence, temperature safety evidence, and physical SPI validation remain false.

Z-015 intentionally performs **zero runtime ADBMS SPI transfers**. Startup prepares the private SPI6 backend and leaves both manual chip selects inactive/high. No wake pulse, PEC, ADBMS6822 protocol, ADBMS6830/APM protocol, acquisition, balancing, or service/CLI SPI operation is present.

## SPI6 architecture implemented

The audited Zephyr v4.4.0 STM32 SPI transaction engine is not used for SPI6. The exact upstream file is `drivers/spi/spi_stm32.c`; for STM32F767 8-bit full-duplex master operation its interrupt completion path contains a BSY wait whose F7/full-duplex execution path has no applicable software escape. `spi_release()` is not an abort primitive. Z-015 therefore uses a small private STM32F767 bounded-polling backend while retaining Zephyr for Devicetree, pinctrl, GPIO, clock-control, reset-control, timing, and build/configuration infrastructure.

Implemented SPI invariants:

- stock `&spi6` Devicetree device remains disabled;
- `CONFIG_SPI=n`, `CONFIG_SPI_ASYNC=n`, `CONFIG_SPI_RTIO=n`, `CONFIG_SPI_STM32_DMA=n`, and `CONFIG_LTO=n`;
- SPI6 pins are PG13 SCK / PG12 MISO / PG14 MOSI;
- manual CS A/B are PE2/PE4 active-low;
- SPI6 input clock contract is 108 MHz, prescaler /256, achieved SCK exactly 421,875 Hz;
- Mode 3, 8-bit, MSB-first, full duplex, software NSS;
- one wrap-safe absolute 500-ms transaction deadline, retained from v2.6.27 for parity;
- SPI6 IRQ is disabled and pending-cleared at initialization/recovery boundaries; `irq_disable()` is paired with CMSIS `NVIC_ClearPendingIRQ()` because Zephyr v4.4.0 has no public `k_irq_clear_pending()` API; no SPI6 interrupt transfer path exists;
- no DMA, RTIO, async callback, `k_poll_signal`, stock `spi_transceive()`, heap allocation, scheduler lock, IRQ lock, or inter-byte sleep/yield is used;
- logical writes still drain RX because SPI6 remains full duplex;
- failure restores both CS lines inactive before recovery;
- recovery resets SPI6 through RCC, reapplies the exact hardware contract, and verifies configuration readback;
- successful recovery returns the operation failure while leaving the adapter READY for later operations; failed recovery latches FAULTED;
- transfer attempts outside READY, including reentry, increment a saturating `integrity_violation_count`;
- the raw transfer API remains private under `drivers/ams/` and has zero runtime callers at Z-015;
- later CLI/service migration remains gated on the single-owner ADBMS request/response path.

## Post-review current-ADC hardening

A second platform review found that the pre-existing current ADC adapter still used `adc_read_async_dt()` + `k_poll_signal` and intentionally became terminal after one timeout. That was safe against uncertain buffer reuse but diverged from the recoverable v2.6.27 HAL behavior and left current sensing permanently unavailable after one transient timeout.

Resetting ADC hardware around the generic async API was rejected because Zephyr retains `adc_context`, completion-signal, buffer-pointer, and ISR ownership beyond an application-side timeout. The current path therefore now uses a private bounded STM32F767 LL polling backend in `drivers/ams/current_adc_stm32.c`.

Current-ADC invariants:

- `CONFIG_ADC=n`, `CONFIG_ADC_ASYNC=n`, `CONFIG_ADC_STM32_DMA=n`;
- generic Zephyr ADC1/ADC2/ADC3 devices remain disabled;
- HIGH = PA3 / ADC1_IN3, LOW = PC0 / ADC2_IN10;
- PCLK2 = 108 MHz, common ADC prescaler /6, achieved ADC clock 18 MHz;
- 12-bit conversion, 480-cycle sample time, software-triggered single conversion;
- exact v2.6.27 5-ms poll semantics are preserved, including strict `elapsed > timeout`, EOC recheck at the timeout boundary, and STRT+EOC clear on success;
- ADC shared IRQ is disabled and pending-cleared before and after recovery reset using Zephyr `irq_disable()` plus CMSIS `NVIC_ClearPendingIRQ()`;
- the STM32F767 common ADCRST is used for recovery, therefore ADC3 is contractually frozen disabled;
- timeout/conversion-path failure resets and fully reconfigures ADC1/ADC2, then returns the original sample error with the adapter READY if recovery succeeds;
- only recovery/reconfiguration failure becomes terminal FAULTED;
- HIGH-before-LOW ordering is preserved and LOW failure never fabricates a coherent pair.

## Fan-PWM review hardening

The six fan outputs remain mapped through TIM3/TIM4/TIM5. One readiness/clock check per controller is intentional because Zephyr `pwm_is_ready_dt()` is device/controller readiness and `pwm_get_cycles_per_sec()` is timer-controller clock state, not per-channel state.

The review did find that global `CONFIG_PWM_CAPTURE=y`, required by IMD TIM2 capture, causes the STM32 PWM driver to install capture IRQ infrastructure for output-only TIM3/TIM4/TIM5 as well. The fan adapter now explicitly disables those three IRQs and clears their pending NVIC latches with CMSIS `NVIC_ClearPendingIRQ()` after initialization. TIM2 remains untouched for IMD capture.

Existing timer/period oracle parity remains unchanged: period request 3361 reproduces ARR=3360 and 100% duty preserves CCR=3360.

## Other platform boundaries re-reviewed

IMD TIM2 capture, BMS_OK fail-low, IWDG, GPIO, clock-control, and reset-control were re-reviewed for the same abandoned-async-state, unbounded-wait, false-recovery, or authority-leak classes. No comparable open defect was found in the currently exercised paths. No authority was promoted.

## Host/SIL depth

The canonical runner executes the exact production SPI6 and current-ADC adapters against fake external Zephyr/STM32 seams and reruns retained Z-001..Z-014 regressions.

Focused evidence includes:

- SPI engine optimization/model campaign across `-O0`, `-O1`, `-O2`, `-O3`, `-Os`: **1,000,000 randomized transport operations**;
- production SPI adapter randomized campaign: **25,000 transactions / 250,013 checks, zero failures** plus directed timeout/recovery/reentry/configuration scenarios;
- private current-ADC adapter: **285,756 checks**, including exact timeout-boundary, high/low timeout recovery, recovery failure, reentry, and **285,584 randomized stress checks**;
- fan control **688 checks** and fan PWM adapter **150,683 checks**;
- retained watchdog exact differential: **2.5 million policy decisions + 2.5 million heartbeat operations**;
- retained current-window, estimator, IMD, measurement, BMS_OK, fatal-path, and integrated Z-014 system SIL regressions;
- strict-warning builds, ASan, UBSan, GCC `-fanalyzer`, Clang static analyzer, and requested/performed ThreadSanitizer evidence;
- true null-platform `ams_core` build with 16 portable core translation units.

## Mutation/negative controls

The retained Z-014 mutation suite rejects **18** unsafe runtime/watchdog/authority changes.

The expanded Z-015 suite rejects **47** unsafe changes. In addition to the original SPI ownership/mode/clock/recovery/caller/capability controls, it now rejects generic ADC enablement, ADC async/DMA ownership, ADC3 activation despite common reset ownership, ADC reset/IRQ/pending-clear/recovery drift, terminal-on-first-timeout regression, HAL timeout-boundary drift, missing timeout EOC recheck, missing STRT clear, polling-yield insertion, fan output-IRQ hardening removal, and stale build-manifest claims about async ADC ownership.

## Linked-image caller proof

`check_adbms_spi_contract.py` parses the final `zephyr/zephyr.elf` symbol table. The lifecycle symbols must be present while `ams_adbms_spi_write` and `ams_adbms_spi_write_read` must be absent from the linked Z-015 target image. `CONFIG_LTO=n` is frozen so cross-TU inlining/internalization cannot make the symbol-presence proof ambiguous. A host ELF self-test links both a lifecycle-only image and an actual raw-caller image and verifies that the checker distinguishes them.

## Final canonical post-review run

Command:

```text
python scripts/run_z015_host_validation.py . --require-clang --tsan \
  --report docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_REPORT_FINAL_2026-09-07.json
```

Result:

```text
PASS: complete Z-015 deep host/source/SIL validation
Skipped evidence: none
No target build, hardware test, authority enablement, or Z-016 work was performed.
```

Machine-readable result:

- success: `true`
- stages: **54/54 passed**
- elapsed time: **85.990 s**
- skipped evidence: **none**
- ThreadSanitizer requested/performed: `true` / `true`
- frozen SoP/SoH/fuse source identity checked: `true`
- integrated Z-014 regression system SIL performed: `true`
- target build performed: `false`
- hardware validation performed: `false`
- later migration stage performed: `false`

Final evidence files:

- `docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_CANONICAL_FINAL_2026-09-07.log`
- `docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_REPORT_FINAL_2026-09-07.json`

Evidence SHA-256:

```text
8d223a90fecab6f2538ef0a82e592393f197e7ac75a00a2a9fa797f608e62384  Z015_POST_REVIEW_HAL_HOST_SIL_CANONICAL_FINAL_2026-09-07.log
f8f74b837609d25b84debb10efd58058498434084abc5fc4859cc27568fb6080  Z015_POST_REVIEW_HAL_HOST_SIL_REPORT_FINAL_2026-09-07.json
```

The earlier pre-HAL-review Z-015 evidence remains in the repository as historical evidence but is superseded for closeout status by the two files above.

## Remaining Z-015 gates

This exact source snapshot must now re-earn STM32F767 target status with fresh base and IWDG-enabled builds plus `scripts/check_all_contracts.py` against each build. Previous target-green evidence cannot be inherited because the private ADC backend, fan IRQ hardening, and build-contract metadata changed after the earlier target runs.

Physical SPI waveform, ADC/current-sense, fan PWM, and IWDG validation remain open and are not claimed here. No Z-016 migration work should be inferred from this closeout.

## 2026-09-08 pinned-Zephyr target-build compatibility correction

The first real Zephyr 4.4.0 STM32F767 rebuild exposed a host-test-double fidelity defect: the three IRQ-hardening paths used `k_irq_clear_pending()` and asserted `CONFIG_ARCH_HAS_IRQ_PENDING_OPS`, but neither is part of the pinned v4.4.0 public IRQ surface for this target. The intended safety policy was retained and the implementation changed to Zephyr `irq_disable()` plus CMSIS `NVIC_ClearPendingIRQ((IRQn_Type)irq)`. Host fakes no longer invent the missing Zephyr API/capability, source contracts reject their reintroduction, and mutation `zephyr44_nonexistent_pending_api` proves the guard.

The corrected snapshot was then rerun through the complete canonical host/source/SIL gate:

```text
PASS: complete Z-015 deep host/source/SIL validation
54/54 recorded stages passed
89.231 s
Skipped evidence: none
ThreadSanitizer requested/performed: true / true
```

Superseding compatibility-corrected evidence:

- `docs/migration/evidence/Z015_TARGET_NVIC_COMPAT_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z015_TARGET_NVIC_COMPAT_HOST_SIL_REPORT_2026-09-08.json`
- `docs/migration/Z015_TARGET_BUILD_NVIC_COMPAT_FIX_2026-09-08.md`

The prior 2026-09-07 canonical evidence remains historical. A fresh real target build is still required; this host rerun does not itself make the snapshot target-green.
