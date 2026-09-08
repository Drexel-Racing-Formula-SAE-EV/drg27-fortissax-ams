# AMS portable core

`ams_core` contains platform-neutral DER26/DRG27 AMS behavior and data
contracts that can be exercised by host tests and linked into the Zephyr
application without importing kernel or hardware dependencies.

## Boundary

Code in this directory must not depend on:

- an RTOS kernel or driver API;
- MCU HAL APIs or peripheral registers;
- board pin names;
- dynamic allocation;
- application-thread ownership.

Physical accumulator topology may live here when it is an immutable product
contract rather than a build-profile selection.

## Z-006 foundation

Z-006 established:

- portable scalar types;
- frozen physical topology;
- the `UINT16_MAX` unknown-current-uncertainty sentinel;
- wrap-safe millisecond elapsed-time/freshness helpers.

## Z-007 coherent measurement store

Z-007 ports the frozen reader-pinned double-buffer publication model into the
portable core:

- exactly two static measurement buffers;
- one writer transaction at a time;
- nonzero publication sequences with wrap-safe zero skipping;
- intentional sequence gaps for dropped/aborted writer attempts;
- reader counts that pin a published buffer during copy;
- copy-outside-lock semantics;
- bounded saturating publication-drop accounting;
- explicit writer abort recovery;
- injected short metadata lock operations;
- no heap and no hardware access.

The snapshot carries RAW cell voltage, same-epoch AVG8/IIR candidates,
per-reading ages/usable masks, temperature data, current-window result metadata,
balancing state, and validity flags. RAW cell voltage remains the safety
representation; the filtered/averaged candidates are observational/estimator
inputs only.

The current-window integration and boundary-rotation algorithm is deliberately
left for Z-008 so the v2.6.27 ordering, carried metadata, mixed-range latching,
uncertainty, and calibration-provenance rules can be migrated and tested as one
unit.


## Z-009 estimator core

Z-009 ports the estimator math from the frozen DER26 AMS v2.6.27 /
firmware 0.5.30 oracle without changing the numerical algorithm.

Included:

- P42A OCV/R0/C1/tau1 lookup tables;
- 3-state inner EKF `[SoC, Vp1, Vp2]`;
- full 3x3 covariance including cross-covariances;
- scalar adaptive outer R0 loop;
- adaptive measurement covariance;
- Joseph-form covariance update/repair;
- feed-forward thermal observer;
- startup acquisition/relaxation anchor logic;
- pack, segment, and even-split estimator configuration;
- coulomb-count fallback/reference state;
- estimator summary/status generation;
- estimator-local R0 observation bookkeeping used by the dual-EKF path.

Not included in Z-009:

- the Zephyr estimator thread;
- measurement-store-to-estimator input collection;
- source selection between RAW/AVG8/IIR;
- runtime epoch scheduling or one-sequence-once task ownership;
- HIL CAN parsing;
- general SoH, SoP, or fuse engines;
- any safety or BMS_OK authority.

`ams_soc_ekf.c`, `ams_estimator_lut.c`, and their public headers are
byte-for-byte oracle copies after only the include-path substitutions required
to make them part of `ams_core`. `ams_estimator_config.h` reproduces the
estimator-local topology identifiers and the v2.6.27 default BENCH -> PACK
topology without importing the legacy build-profile header.

See `estimator/ORACLE_PROVENANCE.md` for source hashes.

## Z-010 scope

Z-010 ports the exact portable v2.6.27 / firmware 0.5.30 power-health core:

- `ams_sop`: deterministic finite-horizon State-of-Power solver and recovery/slew policy;
- `ams_soh`: capacity/resistance State-of-Health estimator and persistence schema;
- `ams_fuse_observer`: preliminary EAC14-80 thermal-utilization observer.

Only include paths are adapted to the portable `ams_core` layout. The Z-010
contract reverses those substitutions and verifies SHA-256 equality against the
frozen v2.6.27 source.

Z-010 intentionally does not port `ams_power_state`, `ams_power_strategy`, or
`ams_power_can`. Those combine measurement/estimator/mission/CAN integration and
belong to later integration and CAN phases. No power-core algorithm is connected
to a Zephyr thread in Z-010.

## Z-011 current sensor and fault core

Z-011 adds the portable v2.6.27 DHAB current-sensor processing and current-fault
policy. The sensor object owns conversion, range hysteresis, cross-range
plausibility, deadband, telemetry filtering, zero calibration, persistent
calibration provenance, uncertainty, and validity. It does not own an ADC
handle or any Zephyr/HAL object.

Raw ADC acquisition is supplied by `drivers/ams/current_adc_stm32.c`. After the
Z-015 HAL-driver audit it uses a private bounded STM32F767 polling backend so a
transient conversion timeout can reset/reconfigure the common ADC block and
retry on the next scan without leaving Zephyr async/ISR ownership behind. The
adapter is deliberately policy-free and the live current thread remains
unintegrated until Z-022 proves the v2.6.27 current-window mutex/publication
ordering. See `docs/migration/Z011_CURRENT_ADC_PARITY.md`.

## Z-012 fan thermal-control core

Z-012 ports the exact v2.6.27 fan thermal policy into `ams_core` and connects
it to a policy-free Zephyr PWM adapter for the six DER26 fan zones. The
portable policy preserves the live-temperature authority, fail-max paths,
hysteresis, charge floor, and thermal ramp. The application currently has no
ported temperature producer, so the real fan worker deliberately presents
invalid temperature evidence and the oracle policy requests 100% cooling.

The PWM adapter reproduces the original TIM3/TIM4/TIM5 channel mapping,
108 MHz timer clock, prescaler 0, ARR=3360 period, active-high PWM1 polarity,
and legacy percent-to-CCR mapping. See `fan/ORACLE_PROVENANCE.md` and
`docs/migration/Z012_FAN_PWM_PARITY.md`.

## Z-013 IMD capture core

Z-013 ports the v2.6.27 insulation-monitoring-device status/freshness logic
into `ams_core` and connects it to a narrow Zephyr TIM2 PWM-capture + PC5
`OK_HS` adapter. The portable core preserves coherent period/high-time tuples,
250 ms capture freshness, unsigned tick-wrap behavior, duty/frequency checks
and the original 10 Hz status encoding.

The real Zephyr IMD thread runs at 10 Hz and forces BMS_OK low on any invalid,
stale, non-NORMAL, or low-`OK_HS` result before publishing its software
heartbeat. Z-013 deliberately keeps `CONFIG_AMS_IMD_TARGET_VALIDATED=n`; real
hardware polarity/status validation remains a release gate. See
`imd/ORACLE_PROVENANCE.md` and `docs/migration/Z013_IMD_CAPTURE_PARITY.md`.
