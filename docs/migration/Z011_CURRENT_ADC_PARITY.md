# Z-011 — DER26 current ADC / DHAB parity

Status in this candidate: **host/SIL validated with post-Z-015 HAL hardening; target rebuild pending**.

Z-011 ports the DER26 pack-current acquisition substrate while intentionally
leaving the live 20 ms current-thread transaction for Z-022. The purpose is to
separate three questions that must not be blurred together:

1. does Zephyr acquire the same physical ADC channels with the same conversion
   configuration and bounded completion policy;
2. does the portable DHAB/current-fault code produce the same state as the
   frozen FreeRTOS implementation; and
3. later, does the Zephyr current thread reproduce the exact mutex/publication
   ordering required by the safety architecture.

Only (1) and (2) are Z-011.

## Frozen oracle

Behavioral oracle: **DER26 AMS v2.6.27 / FW0.5.30**, package
`DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`.

The exact files used to establish the current-path contract were:

| Oracle file | SHA-256 |
|---|---|
| `AMS/Core/Inc/ext_drivers/current_sensor.h` | `5d713f2d484078503e3769cdfb37481766507ff9199926a9fde5b498eb7c8906` |
| `AMS/Core/Src/ext_drivers/current_sensor.c` | `01883c0466375cfb97446b141496f0c2a63037aad585329718ba33742c8eb12a` |
| `AMS/Core/Inc/ext_drivers/current_fault.h` | `12c94c1da87ea7de9ba40a634fb0f146af48a83485022e861ea5907b2a8c9d7a` |
| `AMS/Core/Src/ext_drivers/current_fault.c` | `613291629395912918b39f2aa661dbd15eb9a7056dc13f147353d905334f7cfb` |
| `AMS/Core/Src/tasks/current_task.c` | `b0f26b71426af014f94fe2803f6654cf9f2bda0a8af68ed1612ff086593ab0f2` |
| `AMS/Core/Inc/ext_drivers/stm32f767z.h` | `0977dc51084c07f028072475352aa350a50c17a13d59deaa16eb9cf15ffdeeab` |
| `AMS/Core/Src/ext_drivers/stm32f767z.c` | `13928fb07994baae307f81eaa0752dea3ce3cf03aae8ccf4443b3ea926bd8f29` |
| `AMS/Core/Src/board.c` | `5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df` |
| `AMS/Core/Src/main.c` | `ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84` |

Do not substitute current DER26 `main` for this oracle.

## Physical acquisition contract

| Signal | Physical pin | ADC | channel | DHAB range |
|---|---|---|---:|---:|
| `C_SENSE_H` | PA3 | ADC1 | 3 | ±800 A |
| `C_SENSE_L` | PC0 | ADC2 | 10 | ±50 A |

The transaction order is **HIGH first, LOW second**, matching
`current_sensor_read_adc()` in v2.6.27. A HIGH failure suppresses the LOW read.
A LOW failure may leave a fresh HIGH sample for diagnostics, but the pair is not
coherent and cannot become a valid current sample.

ADC configuration is frozen to the FreeRTOS setup:

- 12-bit conversion;
- PCLK2 = 108 MHz;
- synchronous ADC clock;
- ADC prescaler = 6, therefore 18 MHz ADC clock;
- 480 ADC acquisition cycles;
- software-triggered single conversion;
- no continuous conversion;
- no DMA;
- no oversampling;
- each channel is configured immediately before its conversion.

## Current ADC platform design — post-Z-015 HAL hardening

The original Z-011 adapter used Zephyr 4.4 `adc_read_async_dt()` plus a static
`k_poll_signal` and permanently faulted the adapter after a 5 ms application
timeout. A later HAL/driver audit during Z-015 showed that this was safe but too
pessimistic, and that simply resetting ADC hardware around the Zephyr async
driver would not be sufficient.

The pinned Zephyr STM32 ADC driver retains software transaction ownership in
`adc_context`, including the active buffer pointer and ISR completion state. An
external application timeout therefore does not prove that the driver will no
longer write the old buffer or raise the old completion object. The hardened
implementation removes that uncertain lifetime instead of attempting to cancel
it.

Current architecture:

- `CONFIG_ADC=n`, `CONFIG_ADC_ASYNC=n`, and `CONFIG_ADC_STM32_DMA=n`;
- ADC1, ADC2, and ADC3 remain disabled as generic Zephyr ADC devices;
- `drivers/ams/current_adc_stm32.c` privately owns ADC1/ADC2 through STM32 LL;
- no ADC ISR, DMA, async callback, `adc_context`, or `k_poll_signal` is used;
- ADC1/ADC2 shared IRQ 18 is disabled and pending-cleared at init and recovery;
- the common STM32F767 ADCRST boundary is used for recovery; ADC3 must remain
  disabled because the same RCC reset affects all three ADC instances;
- exact clock/configuration readback is verified after every reset.

The per-conversion sequence now mirrors the v2.6.27 HAL contract directly:

`configure fixed channel` -> `clear EOC/OVR` -> `enable ADC` -> `3 us stabilize`
-> `software start` -> `bounded poll` -> `clear STRT+EOC` -> `read DR` ->
`disable ADC`.

The 5 ms timeout semantics are intentionally the exact F7 HAL semantics, not a
generic Zephyr timeout approximation. The timeout clock starts after conversion
start; timeout is committed only when unsigned elapsed time is **greater than**
5 ms; and EOC is rechecked before returning timeout so preemption/boundary
completion cannot create a false timeout. On success, both STRT and EOC are
write-zero-cleared before the data register is consumed, matching
`HAL_ADC_PollForConversion()`.

### Timeout recovery policy

A transient timeout or conversion-path error is now recoverable:

1. disable and pending-clear the shared ADC IRQ;
2. disable ADC1/ADC2;
3. toggle the common ADC RCC reset;
4. disable/pending-clear the shared IRQ again because RCC reset does not own the
   NVIC pending latch;
5. reapply the full frozen ADC contract;
6. verify register readback;
7. return the original operation error while leaving the adapter READY.

Only reset/reconfiguration/readback failure latches the adapter `FAULTED`. This
restores the important v2.6.27 availability behavior: one conversion timeout is
a bad sample, not a permanent loss of pack-current measurement.

HIGH remains first and suppresses LOW on any HIGH failure. A LOW failure may
leave the already-completed HIGH count marked fresh for diagnostics, but the pair
remains incomplete and has no coherent current-measurement authority.

## Exact portable DHAB behavior

The conversion and state machine remain the v2.6.27 behavior:

- nominal ADC Vref: 3.3 V; accepted configuration 2.8–3.6 V;
- nominal DHAB supply: 5.0 V; accepted configuration 4.5–5.5 V;
- nominal DHAB offset: one half of sensor supply;
- AMS divider: 100 kΩ / 150 kΩ, gain 0.6;
- ±50 A sensitivity: 40 mV/A at 5 V;
- ±800 A sensitivity: 2.5 mV/A at 5 V;
- ADC-count plausibility: invalid below 100 or above 3800;
- sensor output valid range: 0.20–4.80 V;
- clamp classification: ≤0.30 V or ≥4.70 V;
- 50 A → 800 A transition: above 45 A;
- 800 A → 50 A return: at or below 38 A;
- channel comparison begins at 10 A on the 50 A path;
- allowed disagreement: 7.5 A + 15% of `abs(I50)`;
- 50 A deadband: 0.25 A;
- 800 A deadband: 2.0 A;
- telemetry IIR alpha: 0.25;
- live selected current remains the safety/current-window authority;
- IIR-filtered current remains telemetry only;
- an invalid read keeps numeric history but clears measurement authority.

## Calibration/provenance parity

The 44-byte calibration record, schema, little-endian field-by-field CRC32,
zero-capture limits, uncertainty gates, record provenance, and invalidation on
reference changes are unchanged from v2.6.27.

Important limits:

- magic `0x4943414C` (`ICAL`);
- schema 1;
- CRC polynomial `0xEDB88320`;
- zero capture: ±5 A on 50 A, ±25 A on 800 A;
- confident uncertainty: ≤500 mA on 50 A, ≤5000 mA on 800 A;
- calibration temperature metadata: -40.0°C through +120.0°C;
- a service zero capture alone does not establish formal calibration
  provenance.

## Current-fault parity

The portable current-fault module is the exact v2.6.27 policy with only include
path adaptation. It retains positive-current-is-discharge and nominal-period
accumulation semantics.

| Mode | warning | normal trip | fast trip | extreme |
|---|---:|---:|---:|---:|
| discharge | 70 A | 85 A / 500 ms | 120 A / 100 ms | 240 A immediate |
| charge | 10.5 A | 12 A / 500 ms | 15 A / 100 ms | 30 A immediate |
| regen | 20 A | 25 A / 500 ms | 30 A / 100 ms | 50 A immediate |
| precharge | 0.8 A | 1.2 A / 200 ms | 2.0 A / 40 ms | — |

Sensor invalidity keeps the two-layer v2.6.27 behavior:

- current measurement invalidity is immediately unusable for future readiness;
- diagnostic sensor-fault confirmation uses 250 ms startup-ignore + 250 ms
  confirmation = 500 ms.

The regen placeholder warning at 5–20 A is retained.

## Z-011 SIL evidence

Portable current-sensor differential SIL compared the adapted implementation
against the exact v2.6.27 source:

- 100,000 stateful operations/seed;
- 5 deterministic seeds;
- `-O0`, `-O1`, `-O2`, `-O3`, `-Os`;
- **2.5 million current-sensor differential operations**;
- exact trace match at every optimization level.

Portable current-fault differential SIL used the same matrix:

- **2.5 million current-fault differential operations**;
- exact trace match at every optimization level.

Combined exact differential coverage: **5 million operations**.

The production `current_adc_stm32.c` is compiled against a fake
Zephyr/STM32-LL backend. The adapter SIL executes 50,000 randomized transactions
plus directed init, HIGH timeout, LOW timeout, exact HAL timeout-boundary,
recovery-failure, IO-fault, and reentry scenarios. It proves transient timeout
recovery, HIGH-before-LOW suppression, common-reset reconfiguration, durable
integrity-violation evidence, and exact STRT/EOC success-flag clearing.

These are firmware/SIL results only. They do not prove STM32 analog accuracy,
real conversion latency, ADC interrupt behavior, electrical noise, DHAB sign,
or current-sensor calibration on hardware.

## Z-022 deferred integration contract

Z-011 **must not** call `ams_current_adc_read_pair()` from the live Zephyr
current thread. The thread remains a non-safety-evidence placeholder.

Z-022 will reproduce the FreeRTOS transaction under the 20 ms priority-
inheritance current-window mutex:

1. acquire current-window mutex;
2. begin fresh ADC transaction;
3. acquire HIGH then LOW;
4. feed counts into portable current sensor;
5. convert;
6. capture **completion** timestamp;
7. update current fault using 20 ms nominal sample period;
8. merge uncertainty/range/calibration metadata;
9. update current window;
10. atomically publish scalar safety state;
11. release mutex;
12. log newly latched faults;
13. force BMS_OK low on invalid/current fault;
14. publish current heartbeat.

ADBMS rotation later takes that same mutex at the voltage-complete boundary.
This is intentionally deferred so Z-011 cannot accidentally reintroduce the
historical current-window boundary race while peripheral bring-up is still
being validated.

## Z-011 non-goals

- no DMA;
- no ADC oversampling;
- no current threshold tuning;
- no sign-convention change;
- no Vrefint compensation;
- no fabricated live 5 V DHAB supply measurement;
- no automatic zero calibration;
- no replacement of invalid current by zero;
- no filtered current on a safety path;
- no current-window mutation in the ADC adapter;
- no BMS_OK assertion;
- no current-thread integration;
- no CAN/SPI enablement.
