# Z-012 fan PWM and thermal-safety parity

Oracle: DER26 AMS v2.6.27 / firmware 0.5.30.

Z-012 ports the real v2.6.27 fan thermal policy and the six-zone STM32 fan PWM
output path. It does not invent a new variable-speed controller: v2.6.27
already contains a temperature-dependent fan controller with hysteresis and
fail-max behavior.

## Safety boundary

Z-012 keeps BMS_OK assertion and balancing authority compile-time disabled.
CAN1, SPI6, TIM2/IMD capture, and IWDG remain outside this stage. Therefore a
fan migration defect cannot grant vehicle authority in the Z-012 image.

The fan software heartbeat becomes real liveness evidence only because the
placeholder has been replaced by an actual six-zone actuation attempt. It is
not evidence that a physical fan is rotating; the DER26 hardware has no fan
tachometer/airflow feedback.

## Exact v2.6.27 thermal policy

The control input is the **live validated maximum cell temperature**. Filtered
temperature remains telemetry-only and is not allowed to delay cooling.

- ramp start: 35 C
- full-cooling threshold: 50 C
- minimum running command: 25%
- off hysteresis: 3 C, therefore off at/below 32 C
- CHARGE warm minimum: 35%
- legacy `STATE_CHARGE` numeric value: 2 (preserved in portable policy)

Fail-max precedence is preserved:

1. NULL/invalid/untrusted temperature, read fault, zero usable temperature
   sensors, or non-finite live maximum -> 100%, `TEMP_INVALID`;
2. confirmed temperature fault -> 100%, `TEMP_FAULT`;
3. `temp_fan_max` or max temperature >= 50 C -> 100%, `MAX_TEMP`;
4. otherwise apply the exact v2.6.27 off/hysteresis/charge/ramp policy.

Temperature acquisition is not integrated in Z-012. The Zephyr fan worker
therefore explicitly supplies invalid/untrusted temperature evidence. That
causes the exact oracle policy to request 100% cooling; Z-012 never fabricates
a nominal temperature to make the fans turn off.

## Six-zone hardware contract

| Zone | Pin | Timer/channel |
|---|---|---|
| 1 | PA7 | TIM3 CH2 |
| 2 | PB1 | TIM3 CH4 |
| 3 | PD14 | TIM4 CH3 |
| 4 | PD15 | TIM4 CH4 |
| 5 | PA0 | TIM5 CH1 |
| 6 | PA1 | TIM5 CH2 |

All use active-high PWM1, an APB1 timer clock of 108 MHz, prescaler 0, and the
legacy `ARR=3360` period. The physical period is therefore 3361 timer clocks,
or about 32133.2937 Hz.

Zephyr's STM32 PWM driver subtracts one from `period_cycles` when programming
an up-counter ARR. Z-012 therefore requests 3361 cycles. It also reproduces the
legacy percent-to-CCR function exactly, including the unusual legacy 100%
command of CCR=3360 rather than 3361.

## Startup and failure semantics

The FreeRTOS implementation has two different failure classes and Z-012 keeps
that distinction:

### Platform/timer initialization failure

A failure while initializing TIM3/TIM4/TIM5 reaches the legacy
`Error_Handler()`. In Z-012, an unavailable PWM timer, failure to query its
clock, or a timer clock that does not equal the expected 108 MHz makes
`ams_fan_pwm_init()` fail. `main()` then panics, and the existing fatal handler
forces BMS_OK physically low before halting.

### Per-channel PWM start/write failure

Legacy `fan_init()`/`set_fan_percent()` failures are process/output faults, not
software-integrity resets. Z-012 likewise:

- attempts an explicit 0% command on every zone during fan adapter init;
- records a startup failure mask but does not panic for a channel-write error;
- publishes the startup fan process fault before application workers start;
- retries each zone from the 5 Hz fan worker;
- attempts all six zones even if an earlier zone fails;
- increments the set-failure counter saturating at UINT32_MAX;
- changes the diagnostic reason to `DRIVER_FAULT` for that iteration;
- still publishes the fan heartbeat after the six-zone attempt.

This preserves the v2.6.27 distinction between a process/output fault and a
software-liveness failure.

## Scheduling parity

The legacy fan task captures its iteration entry tick, executes all fan work,
then calls `osDelayUntil(entry + 200 ms)`. If work overruns the period, the
delay returns immediately and the next iteration reanchors to its new entry
rather than skipping an additional release.

The Z-012 fan worker has a dedicated release policy to reproduce that behavior.
Other migration placeholders retain their existing absolute-release/skip
policy until their own migration stages.

## Heartbeat semantics

The heartbeat is published only after the real thermal-policy evaluation and
all six PWM command attempts. It proves that the fan software task executed.
It does **not** prove fan rotation, airflow, wiring continuity, MOSFET health,
or 12 V fan supply health.

## Intentional platform adaptation requiring hardware validation

FreeRTOS/HAL configures the timer GPIO alternate functions during timer MSP
initialization; Zephyr's STM32 PWM device applies pinctrl during its driver
initialization and enables PWM channels on the first `pwm_set_cycles()` call.
The steady-state period, polarity, PWM mode, compare mapping, and channel
wiring are source-contract matched, but SIL cannot prove the reset-to-AF
transition is glitch-free.

Before Z-012 hardware parity is called green, scope all six physical fan gate
signals and verify:

- reset and boot remain inactive until the intended fan command;
- no unsafe pinctrl/first-enable glitch;
- approximately 32133.2937 Hz at an intermediate test duty;
- active-high polarity;
- exact 0%, intermediate, and legacy 100% behavior;
- each shared timer pair can change one channel without corrupting its sibling;
- reset/reboot transitions are safe.

Until that physical validation is complete, Z-012 may be code-green but not
hardware-parity-green.
