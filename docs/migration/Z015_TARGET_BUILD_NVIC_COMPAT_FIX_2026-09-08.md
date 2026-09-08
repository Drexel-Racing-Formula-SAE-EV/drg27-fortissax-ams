# Z-015 Zephyr v4.4 Target-Build NVIC Compatibility Correction

Date: 2026-09-08

Status: **source/host/SIL fix complete; fresh STM32F767 base + IWDG target builds required.**

## Trigger

The first target rebuild of the post-HAL-hardening snapshot failed while compiling `fan_pwm_zephyr.c`, `current_adc_stm32.c`, and `adbms_spi_stm32.c`. All three failures had the same root cause:

- `CONFIG_ARCH_HAS_IRQ_PENDING_OPS` is not enabled/provided for this pinned Zephyr v4.4.0 STM32F767 configuration;
- `k_irq_clear_pending()` is not declared by pinned Zephyr v4.4.0 `include/zephyr/irq.h`.

The host fake IRQ surface had incorrectly invented both the capability symbol and helper, so the previous host-only campaign could not expose the target compatibility error. This was a test-double fidelity defect, not a failure of the intended NVIC safety policy.

## Pinned-source audit

Pinned Zephyr v4.4.0 public `include/zephyr/irq.h` exposes `irq_enable()`, `irq_disable()`, and `irq_is_enabled()` through the architecture API but no public pending-clear helper. The Cortex-M implementation maps IRQ disable directly to `NVIC_DisableIRQ()`. The STM32F7 SoC header includes `stm32f7xx.h`, which provides the Cortex-M7 CMSIS NVIC interface.

Because these three adapters are deliberately STM32F767/Cortex-M7-specific at this migration boundary, pending-latch clear is now performed explicitly with:

```c
irq_disable(irq);
NVIC_ClearPendingIRQ((IRQn_Type)irq);
```

This preserves the audited rule that an IRQ line is disabled and its already-latched NVIC pending state is cleared before/after peripheral recovery, without relying on a Zephyr API that does not exist in v4.4.0.

## Files corrected

Production:

- `drivers/ams/adbms_spi_stm32.c`
- `drivers/ams/current_adc_stm32.c`
- `drivers/ams/fan_pwm_zephyr.c`

Host test doubles and contracts were corrected so they no longer define or expect the nonexistent v4.4 API/capability. The source checker now rejects reintroduction of either `k_irq_clear_pending` or `CONFIG_ARCH_HAS_IRQ_PENDING_OPS`. A dedicated mutation named `zephyr44_nonexistent_pending_api` proves the guard.

## Revalidation after correction

Changed production paths were rerun through directed/randomized SIL, ASan, UBSan, GCC `-fanalyzer`, and Clang static analysis. The Z-015 mutation suite rejects **47** unsafe mutations after adding the v4.4 API-compatibility negative control; the retained Z-014 mutation suite still rejects **18**.

No target-build success is claimed by this document. The corrected package must be rebuilt on the real Zephyr 4.4.0 STM32F767 workspace, then `scripts/check_all_contracts.py` must pass on both base and IWDG images before target-green status is restored.
