# Z-012 fan-control oracle provenance

Z-012 is based on the frozen DER26 AMS FreeRTOS oracle:

- package: `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`
- firmware: `0.5.30`
- migration stage: `Z-012`

The production thermal-control function in `ams_fan_control.c` is a behavioral
port of the v2.6.27 `fan_task.c` policy. Only platform/type/namespace
substitutions needed to detach the pure policy from `app_data_t` were made.

## Frozen source hashes

| Oracle source | SHA-256 |
|---|---|
| `AMS/Core/Src/tasks/fan_task.c` | `050e15dafbe95433a254f6f994cfb262dc388c32098a0a7849f64044ec782f63` |
| `AMS/Core/Src/ext_drivers/fans.c` | `4793d9df6130d43e0f079b295b478b99a5452e6e407e76f1101b2392072aaf02` |
| `AMS/Core/Src/board.c` | `5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df` |
| `AMS/Core/Src/main.c` | `ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84` |

## Canonical policy hashes

The Z-012 contract checker removes comments/whitespace and reverses the
approved namespace substitutions before hashing the two safety-relevant policy
functions:

| Canonical function | SHA-256 |
|---|---|
| `fan_temp_for_control` | `035abe2a419462eb4771523a5a74c4bfc5fc5b72a558b63470d8a4f48c8492a2` |
| `fan_percent_from_temp` | `3f464250367b7e3c29a2d0b89303a1e22910a16da7972edd7dcb672bd32dcca1` |

These canonical hashes are enforced by `scripts/check_fan_pwm_contract.py`.

## Exact hardware behavior carried forward

- Fan 1: PA7 / TIM3 CH2
- Fan 2: PB1 / TIM3 CH4
- Fan 3: PD14 / TIM4 CH3
- Fan 4: PD15 / TIM4 CH4
- Fan 5: PA0 / TIM5 CH1
- Fan 6: PA1 / TIM5 CH2
- APB1 timer clock: 108 MHz
- timer prescaler: 0
- up-counter ARR: 3360
- PWM mode: PWM1
- output polarity: active high
- maximum compare value: 3360
- legacy `STATE_CHARGE` numeric value: 2

The Zephyr STM32 PWM driver programs `ARR = period_cycles - 1` for an
up-counter, so Z-012 requests a 3361-cycle period to reproduce the legacy
`ARR=3360` timer period exactly.

The legacy 100% command writes CCR=3360 with ARR=3360. Z-012 intentionally
preserves that behavior rather than replacing it with an idealized
`pulse_cycles=period_cycles` constant-high command.
