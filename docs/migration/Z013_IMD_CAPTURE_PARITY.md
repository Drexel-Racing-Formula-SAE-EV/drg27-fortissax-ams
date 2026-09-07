# Z-013 IMD capture parity — v2.6.27 / FW0.5.30

Z-013 ports the DER26 insulation-monitoring-device (IMD) acquisition path from
FreeRTOS/HAL to Zephyr while keeping BMS_OK assertion and balancing authority
compile-time disabled.

Oracle package:

`DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`

Firmware identity: `0.5.30`.

## Frozen oracle source

| File | SHA-256 |
| --- | --- |
| `AMS/Core/Inc/ext_drivers/imd.h` | `81d8dc354098544fc8164049a831eede20f6a265bbc62a8f9076548e26c43180` |
| `AMS/Core/Src/ext_drivers/imd.c` | `b23265087af40afb06a55d56fde018cef656c9260b4a8ce9c38d43489289f73f` |
| `AMS/Core/Inc/tasks/imd_task.h` | `2fd10284eeb450567ffdfddf4927fe4a1e7c9605faf9787097ab9559e71a0b12` |
| `AMS/Core/Src/tasks/imd_task.c` | `c264f5f773d7280e5227925414736e21bc08fd3f660960ec6e37f1ba3bb3e385` |
| `AMS/Core/Src/board.c` | `5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df` |
| `AMS/Core/Src/main.c` | `ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84` |
| `AMS/Core/Src/stm32f7xx_hal_msp.c` | `60147313c3df238de122e7089331f852960392e67f7f2c9600ab1f810ae37302` |

## Physical contract

- PWM/status data input: `PA5 / TIM2_CH1`.
- independent `OK_HS` digital input: `PC5`, active high.
- TIM2 input clock: 108 MHz (`216 MHz SYSCLK`, `APB1 /4`, STM32 timer x2).
- TIM2 prescaler: 0.
- TIM2 counter: 32-bit up counter.
- TIM2 interrupt: IRQ 28, preemption priority 5, matching the v2.6.27 MSP.
- period capture: CH1 direct input, rising edge.
- high-time capture: CH2 indirect input, falling edge.
- slave mode: RESET on TI1FP1 rising edge.
- input-capture prescaler: DIV1.
- input filter: 0.

Zephyr 4.4's STM32 two-channel PWM-capture mode uses the same CH1 direct /
CH2 indirect / reset-mode topology. Z-013 therefore uses the normal Zephyr PWM
capture driver instead of a private register-level TIM2 driver.

The board deliberately omits `four-channel-capture-support`, because the
source behavior requires the hardware-reset two-channel PWM-input mode.

## Portable IMD algorithm

`lib/ams_core/imd/ams_imd.c` owns only platform-independent logic:

1. capture tuples are published coherently as `(high, total, timestamp)`;
2. no tuple means invalid/fail-closed;
3. tuple age `<= 250 ms` is fresh; `> 250 ms` is invalid;
4. unsigned age arithmetic preserves tick-wrap behavior;
5. total count must be nonzero;
6. high count must not exceed total count;
7. duty is `high * 100 / total`;
8. frequency is `108 MHz / total` at the target clock;
9. nonfinite or out-of-range duty/frequency fails closed;
10. status is the original 10 Hz encoding:
    `int(0.5 + frequency_hz / 10)` with explicit 0..5 range checking;
11. status values remain exactly:
    - 0 short-to-chassis;
    - 1 normal;
    - 2 undervoltage;
    - 3 speed/start;
    - 4 device error;
    - 5 ground fault;
    - 0xff unknown;
12. IMD is healthy only when the PWM read is valid, `OK_HS` is high and the
    decoded status is `NORMAL`.

The portable implementation was differential-tested against the exact oracle;
platform-only GPIO/timer ownership is intentionally excluded from that math
comparison.

## Zephyr adapter behavior

`drivers/ams/imd_capture_zephyr.c` is deliberately policy-free.

Initialization:

1. verify the TIM2 PWM and PC5 GPIO devices are ready;
2. configure PC5 as input;
3. obtain TIM2 cycles/sec and require exactly 108 MHz;
4. configure channel 1 continuous `BOTH` PWM capture, normal polarity;
5. enable capture.

Configuration/device/clock failures are startup-integrity failures and return
an error to `ams_threads_start()`, which reaches the existing panic/fail-low
path. This corresponds to Cube/HAL TIM2 configuration failures reaching
`Error_Handler()`.

A capture-enable failure is intentionally different: v2.6.27's runtime
`HAL_TIM_*Start*()` result was retained in the IMD object and made the IMD
process fail closed rather than panicking. Z-013 preserves that split: capture
start failure records a soft process fault, the IMD thread runs, reports invalid
and forces BMS_OK low.

A Zephyr capture callback error is treated more conservatively than HAL: it
immediately invalidates IMD authority at the next 10 Hz service cycle instead
of permitting the old tuple to remain usable for the rest of its 250 ms age.
A later clean capture clears that transient adapter fault and publishes a new
coherent tuple.

## Task and safety semantics

The real Zephyr IMD thread runs at the source-equivalent scheduling policy:

- period: 100 ms / 10 Hz;
- relative priority: below fan/AIR and above diagnostics;
- heartbeat timeout: 500 ms;
- startup grace: 3000 ms;
- stack: 1536 bytes, never below the v2.6.27 768-byte equivalent allocation.

Each cycle performs:

1. read/evaluate the latest IMD capture and PC5 status;
2. derive `valid`, status and `ok` exactly as `imd_task_update()`;
3. publish a coherent process snapshot;
4. if `ok == false`, force BMS_OK physically low;
5. only then publish the IMD software heartbeat;
6. delay to `entry + 100 ms`; after an overrun retry immediately and re-anchor
   from the next actual entry, matching the FreeRTOS `osDelayUntil()` behavior.

The heartbeat proves that the IMD software workload ran. It does **not** prove
that the physical IMD or vehicle insulation is healthy; those are represented
separately by `valid`, `ok`, `fault`, status and capture freshness.

## Authority and validation boundary

Z-013 is intentionally a no-authority validation image:

- `CONFIG_AMS_BMS_AUTHORITY=n`;
- `CONFIG_AMS_BALANCE_AUTHORITY=n`;
- `CONFIG_AMS_IMD_TARGET_VALIDATED=n`;
- a C `BUILD_ASSERT` rejects any attempt to claim IMD target validation in this
  stage;
- CAN1 remains disabled;
- SPI6 remains disabled;
- watchdog remains unported/disabled.

Enabling the real IMD workload in Z-013 is **not** equivalent to setting the
v2.6.27 `AMS_IMD_TARGET_VALIDATED` vehicle-release gate. Physical target
validation still requires real hardware measurements of PA5 capture, PC5
polarity, status-frequency decoding, stale behavior and fault cases.

## Z-013 hardware gate, later

Before any vehicle-authority profile may rely on IMD evidence:

- verify PC5 high/low polarity on the actual board;
- measure PA5 PWM frequency/duty for known IMD states;
- confirm all defined 10 Hz status encodings;
- prove capture expires after the 250 ms freshness contract;
- prove unplugged/stuck/invalid PWM fails low;
- prove low `OK_HS` fails low even with a valid NORMAL PWM status;
- verify TIM2 interrupt/load behavior and 10 Hz task WCET;
- confirm no interaction/regression with fan PWM timers or current ADCs.

Until that evidence exists, `AMS_IMD_TARGET_VALIDATED` remains false.

## Reproducible production-adapter host harness

The final Z-013 repository includes `tests/unit/imd/imd_capture_adapter_test.c`
and a minimal fake Zephyr header surface. The harness compiles the actual
`drivers/ams/imd_capture_zephyr.c`; it does not duplicate the adapter algorithm
in a test-only implementation.

The closeout run executed 100,000 randomized adapter operations plus directed
failure/race cases and reported:

- production adapter: **776,535 checks, 0 failures**;
- portable IMD core: **57 checks, 0 failures**;
- GCC `-fanalyzer`: PASS for production core and adapter;
- AddressSanitizer + UndefinedBehaviorSanitizer: PASS for production core and
  adapter;
- Clang static analyzer: PASS for production core and adapter.

The four Z-013 production files remain byte-identical to the original working
candidate used for the existing differential evidence. The closeout change only
adds reproducible host-test infrastructure and documentation.

## Closeout limitation

The repository still requires the STM32F767 Zephyr target build and generated
DTS/map/config contract suite in the real west workspace before Z-013 may be
committed. Host validation cannot substitute for that target gate, and no
physical IMD validation is claimed by this closeout.


### Zephyr 4.4 target-build binding correction

The first target build exposed a devicetree API issue rather than a hardware or
policy defect. The original Z-013 candidate placed `pwms` and `status-gpios` on
an unbound `imd-capture` pseudo-node, so Zephyr generated the node itself but
not the typed phandle/cell macros required by `PWM_DT_SPEC_GET()` and
`GPIO_DT_SPEC_GET()`. The first target-build correction made the properties consumable, and the
subsequent Z-013 architecture hardening formalized them in the typed
`drexel,ams-imd` node.  The production adapter now consumes a `pwm_dt_spec`
for M_HS (TIM2_CH1/PA5) and a `gpio_dt_spec` for OK_HS (PC5 active high).
This changes no IMD algorithm, timing, pin, polarity, or safety semantics.


## First STM32 target-build corrections

The first Zephyr 4.4 target build found two compile-time devicetree integration defects, not algorithm or safety-policy defects.

1. The Z-011 ADC adapter used `0U`/`1U` as arguments to Zephyr `*_BY_IDX` macros. Those macros paste the index token into generated identifiers, producing nonexistent `IDX_0U`/`IDX_1U` names. The indexes are now bare `0` and `1`; HIGH remains ADC1_IN3 and LOW remains ADC2_IN10.
2. The initial IMD DTS used `pwms`/`status-gpios` on an unbound pseudo-node. Zephyr emitted the node but not the typed phandle-cell macros required by `PWM_DT_SPEC_GET()`. The architecture-hardened form now places both properties on the typed `drexel,ams-imd` consumer node: M_HS references `pwm2` CH1/PA5 and OK_HS references PC5 active high.  The adapter consumes both through Zephyr `dt_spec` helpers.

No current-sensor algorithm, IMD decode logic, pin assignment, timer clock, IRQ priority, polarity, timeout, or fail-low policy changed.
