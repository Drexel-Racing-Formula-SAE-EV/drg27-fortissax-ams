# Z-015 Post-Review STM32/Zephyr HAL Driver Audit

Date: 2026-09-07

Status: **source architecture + canonical host/SIL GREEN after hardening; target and physical validation remain open.**

This review was triggered after the Z-015 private SPI6 closeout by a second independent review of the fan PWM and current ADC adapters. The review deliberately re-applied the SPI6 lesson to every currently live AMS hardware boundary: an application timeout is not enough if a generic driver still owns callback, buffer, IRQ, or peripheral state after the caller returns.

No Z-016 ADBMS6822/isoSPI protocol work is included. BMS_OK assertion authority and balancing authority remain disabled.

## Pinned upstream basis

The upstream behavioral audit is against the Zephyr 4.4.0 revision pinned by this workspace. Relevant upstream files are:

- `drivers/adc/adc_stm32.c`
- `drivers/adc/adc_context.h`
- `drivers/pwm/pwm_stm32.c`
- `drivers/watchdog/wdt_iwdg_stm32.c`
- `kernel/poll.c`
- STM32F7 DTS and STM32F7 LL/HAL ADC/RCC headers from the pinned `hal_stm32` module.

The frozen application oracle remains DER26 AMS v2.6.27 / FW 0.5.30.

## Finding 1 — current ADC timeout was safe but unnecessarily terminal

The earlier Z-011 adapter used `adc_read_async_dt()` plus a `k_poll_signal`. If the application-side 5 ms poll timed out, it permanently faulted the current ADC adapter until reboot. This prevented uncertain storage reuse, but diverged from v2.6.27, where `HAL_ADC_PollForConversion(..., 5)` can fail one sample and a later scan can retry.

The availability consequence is material because current acquisition feeds later DHAB selection, current-fault policy, estimator inputs, coulomb counting, SoP, and fuse observation once those migration gates are live.

### Why reset-around-async is not a sufficient fix

The pinned STM32 Zephyr ADC driver retains transaction state inside `adc_context` and `adc_stm32_data`. In asynchronous operation the driver retains the completion signal and the conversion buffer pointer, while the ADC ISR later writes conversion data through that retained buffer and completes the context. An external timeout does not cancel that ownership.

Therefore this sequence is rejected:

`application timeout -> RCC reset ADC -> reuse Zephyr async ADC context`

RCC reset only owns hardware registers. It does not invalidate the generic driver's software buffer/context ownership or prove that a stale ISR cannot complete against that state.

## Resolution — private bounded STM32F767 current-ADC backend

The current path now follows the same ownership simplification used for SPI6:

- `CONFIG_ADC=n`
- `CONFIG_ADC_ASYNC=n`
- `CONFIG_ADC_STM32_DMA=n`
- ADC1, ADC2, and ADC3 disabled as generic Zephyr ADC devices
- private STM32 LL polling in `drivers/ams/current_adc_stm32.c`
- no ADC ISR, DMA, callback, `adc_context`, `k_poll`, or `k_poll_signal`
- Zephyr remains responsible for Devicetree, pinctrl, clock control, reset control, timing, build configuration, and NVIC helpers.

The typed `drexel,ams-current-sense` node freezes:

- HIGH: PA3 / ADC1_IN3
- LOW: PC0 / ADC2_IN10
- PCLK2 input: 108 MHz
- common ADC prescaler: /6
- achieved ADC clock: 18 MHz
- 12-bit conversion
- 480-cycle sampling time
- software-triggered single conversion
- 5 ms poll timeout.

ADC1 and ADC2 use the same STM32F767 ADC IRQ. That IRQ is disabled and pending-cleared during initialization and recovery.

### Common ADC reset consequence

STM32F767 exposes one common `ADCRST` reset boundary for ADC1/ADC2/ADC3. Current-sense recovery therefore cannot safely coexist with an independently live ADC3 owner. ADC3 is frozen disabled and the contracts reject promotion of ADC3 while this recovery architecture is active.

### Recoverability

Transient timeout or conversion-path failure performs:

1. shared ADC IRQ disable + pending clear;
2. ADC1/ADC2 disable;
3. common ADC RCC reset;
4. shared ADC IRQ disable + pending clear again;
5. exact ADC contract reprogramming;
6. register readback validation;
7. original operation error returned with adapter restored to READY.

Only failure to reset/reconfigure/verify becomes terminal `FAULTED`.

### Exact HAL timeout boundary

A later source-level oracle check found that F7 `HAL_ADC_PollForConversion()` does not use `elapsed >= timeout`. It:

- starts its timeout clock after `HAL_ADC_Start()` returns;
- loops until EOC;
- commits timeout only when unsigned elapsed time is **greater than** the timeout argument;
- rechecks EOC before returning timeout to avoid a false timeout caused by preemption;
- on success clears both STRT and EOC before the data-register read.

The private backend now preserves all of those details, including exact STRT+EOC clearing. This was added as a separate mutation-protected invariant rather than relying on a broad “5 ms timeout” statement.

## Finding 2 — fan PWM readiness concern was not a defect

The review questioned why `ams_fan_pwm_init()` checks fan entries 0, 2, and 4 only. Pinned Zephyr source confirms:

- `pwm_is_ready_dt()` checks the underlying PWM device/controller, not a specific timer channel;
- STM32 `pwm_get_cycles_per_sec()` returns the timer-controller clock and does not have per-channel clock state.

Therefore one readiness/clock check per physical TIM3/TIM4/TIM5 controller is correct. Repeating the same check for the paired channel would add no new evidence.

The adapter documents that assumption explicitly.

## Finding 3 — global PWM capture enabled unused fan NVIC lines

A separate fan issue was found during the same source audit. The image needs `CONFIG_PWM_CAPTURE=y` for the IMD TIM2 input-capture path. In the STM32 PWM driver that global capability causes capture IRQ hookup for every enabled PWM timer device, including output-only TIM3/TIM4/TIM5 used by the six fans.

No fan capture source is enabled, so this was not an observed functional fault. It was nevertheless unnecessary interrupt surface.

`ams_fan_pwm_init()` now disables and pending-clears the output-only timer IRQs after their Zephyr devices initialize:

- TIM3 IRQ 29
- TIM4 IRQ 30
- TIM5 IRQ 50.

TIM2 is intentionally not touched because IMD genuinely uses capture interrupts.

## Other current platform/HAL boundaries re-reviewed

### IMD TIM2 PWM-input capture

No SPI/ADC-style abandoned-transaction lifetime was found. The continuous capture callback points to static runtime state whose lifetime does not expire while capture is active. Driver-reported capture errors are censored and force the next service decision fail-low; a later coherent callback may clear the transient capture fault. GPIO read failure also fails low.

The initialization order can discard a first callback if it lands before `capture_started` is published, but this is fail-closed and is consistent with the oracle's “capture started only after start calls return” semantics. No code change was required.

### Fan PWM output

The existing period/CCR oracle parity remains intact. The adapter requests 3361 timer cycles because the pinned STM32 PWM driver subtracts one before programming ARR, reproducing the v2.6.27 ARR=3360 behavior. The 100% path preserves CCR=3360 rather than using ARR+1.

### BMS_OK fail-low

No async ownership issue was found. The board-level direct PE0 primitive executes independently of normal GPIO-driver health and is invoked before ordinary GPIO initialization and on fatal/error fallbacks. No authority promotion was introduced.

### IWDG

No new issue was found for the configured STM32F767 IWDG path. The irreversible-start ambiguity is already handled by marking the platform terminal before calling `wdt_setup()`, and the pinned driver's status-register update wait is bounded. Safety-supervisor sole-feeder semantics are unchanged.

### GPIO / clock-control / reset-control

The currently used F7 GPIO outputs, peripheral clock-rate queries, and reset-controller calls do not introduce an analogous async caller-lifetime problem. They remain platform mechanisms beneath narrow AMS adapters rather than safety policy in `ams_core`.

### ADBMS SPI6

The earlier Z-015 audit remains unchanged: generic `spi_stm32` does not own SPI6; SPI6 stays disabled as a Zephyr SPI device; the private bounded polling/recovery backend remains the only SPI6 transaction owner and has zero runtime transaction callers at Z-015.

## New/expanded negative controls

The Z-015 source mutation suite now rejects unsafe changes covering both the original SPI substrate and this HAL review, including:

- generic ADC enablement;
- ADC async or DMA enablement;
- generic ADC1 ownership;
- ADC3 promotion despite common ADCRST ownership;
- ADC reset-line drift;
- ADC IRQ enablement or pending-clear removal;
- ADC recovery-reset removal;
- timeout changed back to terminal-on-first-failure;
- HAL timeout boundary changed from `>` to `>=`;
- timeout EOC boundary recheck removal;
- HAL STRT+EOC success-clear removal;
- polling yield insertion;
- fan output-only timer IRQ disable removal;
- fan pending-clear removal.

The retained SPI, authority, watchdog, stack, and source-hygiene mutations remain active as well.

## Scope / claims

This hardening is source/host/SIL green after the final canonical post-review runner; fresh STM32F767 target builds are still required before target-green status.

It does **not** claim:

- physical ADC conversion timing or analog accuracy;
- DHAB calibration/sign validation on hardware;
- physical fan PWM waveform validation;
- physical SPI waveform or ADBMS communication;
- physical IWDG timing;
- ADBMS actor/evidence promotion;
- BMS_OK assertion authority;
- balancing authority;
- Z-016 work.


## Final post-review closeout evidence

The final uninterrupted canonical host/SIL run completed after all ADC timeout-boundary, STRT/EOC, fan IRQ, manifest, and mutation updates:

```text
PASS: complete Z-015 deep host/source/SIL validation
54/54 recorded stages passed
85.990 s
Skipped evidence: none
ThreadSanitizer requested and performed
```

The expanded Z-015 mutation suite rejected **46 unsafe mutations** and the retained Z-014 mutation suite rejected **18**. Final evidence is stored in `docs/migration/evidence/Z015_POST_REVIEW_HAL_HOST_SIL_CANONICAL_FINAL_2026-09-07.log` and `Z015_POST_REVIEW_HAL_HOST_SIL_REPORT_FINAL_2026-09-07.json`. Target build and physical validation remain deliberately unperformed.
