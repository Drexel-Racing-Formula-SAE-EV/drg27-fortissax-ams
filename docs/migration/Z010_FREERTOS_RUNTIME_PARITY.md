# Z-010 FreeRTOS v2.6.27 runtime/safety parity

Oracle: `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`, firmware 0.5.30.

This note separates **ported parity**, **intentional Zephyr differences**, and
**not-yet-ported safety authority**.  The Z-010 image remains compile-time
incapable of asserting BMS_OK or commanding balancing.

## Runtime parity already frozen

| Function | v2.6.27 | Z-010 | Status |
|---|---:|---:|---|
| safety/error task period | 50 ms | 50 ms | matched |
| current task period | 20 ms | 20 ms | matched |
| ADBMS normal period | 100 ms | 100 ms | matched |
| protected CAN service period | 100 ms | 100 ms | matched |
| estimator period | 100 ms | 100 ms | matched |
| fan period | 200 ms | 200 ms | matched |
| legacy AIR period, if enabled | 500 ms | 500 ms | matched |
| IMD period, if enabled | 100 ms | 100 ms | matched |

FreeRTOS uses larger numbers for higher priorities; Zephyr uses smaller
non-negative numbers.  The relative ordering is preserved exactly:

`safety > current > ADBMS > CAN > estimator > fan/AIR > IMD > diagnostics`.

Z-010 Zephyr stacks remain larger than the byte-equivalent v2.6.27 task
allocations.  This is deliberate until target stack-watermark evidence exists;
no stack is allowed below the v2.6.27 allocation by compile-time contract.

## Heartbeat/liveness parity

The initial Z-005 three-period heuristic has been removed.  Z-010 now carries
the exact v2.6.27 heartbeat windows:

- one global startup grace: 3000 ms;
- ADBMS: 3000 ms;
- current: 200 ms;
- temperature: 3000 ms;
- CAN: 2000 ms;
- logger: 2000 ms;
- IMD: 500 ms;
- fan: 1000 ms;
- estimator: 500 ms.

AIR has no heartbeat bit in the v2.6.27 oracle.  The Zephyr diagnostics thread
is not the legacy FreeRTOS logger task, so neither AIR nor diagnostics is
allowed to fabricate safety-liveness evidence.

The temperature heartbeat is frozen as a policy constant but is not yet
produced in Z-010 because the ADBMS acquisition path has not been integrated.
The estimator heartbeat is diagnostic-only while SoP authority is disabled,
matching the v2.6.27 non-vehicle profiles.

The placeholder loops still maintain runtime cycle counters for scheduling
instrumentation, but `safety_evidence_ready` is false for every Z-010 thread.
A placeholder completing on time is therefore explicitly **not** evidence that
its real sensor, transport, actuator, estimator integration, or safety policy is
healthy. Each later integration step must opt into safety evidence only after
real work and its fail-closed checks are present.

## Stage-specific thread enablement

The frozen v2.6.27 code does not start the legacy AIR task when
`AMS_ENABLE_AIR_AUX_FEEDBACK=0`.  Its default/bench profile also has IMD
disabled.  Z-010 therefore creates static AIR and IMD thread objects for
layout/topology review but does not start either placeholder.  A no-op loop
must not be counted as proof that a physical safety input is alive.

IMD becomes eligible to start only when its Zephyr capture adapter/profile is
implemented and validated.  AIR remains disabled until reviewed physical
auxiliary-feedback hardware exists; the current `AIR_CONTROL_MCU` signal is not
physical contactor-position feedback.

## BMS_OK and fatal policy

Z-010 is stricter than the FreeRTOS operating image:

- BMS_OK is forced physically low during earliest application-controlled
  startup;
- BMS_OK assertion authority is rejected at compile time;
- balancing authority is rejected at compile time;
- Zephyr fatal handling forces PE0 low before halting;
- worker/task creation failures reach the same fatal fail-low handler.

Therefore Z-010 cannot accidentally become an authority-capable image while
peripheral/supervisor parity is incomplete.

## Current path parity frozen for later integration

The currently ported current-window algorithm is differential-equivalent to
v2.6.27.  The platform integration has not yet been connected.  The following
v2.6.27 contracts are frozen now for Z-011/Z-022:

- current thread: 20 ms, above ADBMS/CAN priority;
- current-window mutex wait: bounded 20 ms, priority inheritance required;
- ADBMS/shared SPI mutex wait: bounded 500 ms, priority inheritance required;
- current sample timestamp is taken after ADC conversion and while owning the
  current-window mutex;
- both DHAB channels must belong to one coherent fresh acquisition before
  conversion/publication;
- low-range physical channel: PC0 / ADC2_IN10 / +/-50 A;
- high-range physical channel: PA3 / ADC1_IN3 / +/-800 A;
- conservative STM32 sample time: 480 cycles;
- initial Zephyr adapter remains synchronous: no DMA, oversampling, filtering,
  or sample-rate redesign;
- invalid current or confirmed/latched current fault may force BMS_OK low but
  cannot assert it;
- the ADBMS voltage-boundary/current-window rotation must use the same mutex and
  must not rotate using a timestamp captured before a newer current sample was
  published.

The current-sensor conversion, dual-range selection, calibration/provenance,
channel-agreement checks, and `current_fault` policy are **not yet ported** in
Z-010.  They must come from the exact v2.6.27 sources rather than be recreated
from memory during the ADC/integration steps.

## Intentional Zephyr differences

These are not claimed as bit-for-bit scheduler parity:

1. Zephyr priorities use the opposite numeric direction; relative ordering is
   the parity requirement.
2. Zephyr stacks are currently larger.
3. Periodic placeholders use absolute release deadlines and skip missed
   historical releases.  v2.6.27 calls `osDelayUntil(entry + period)` each
   cycle, effectively re-anchoring after an overrun.  Absolute scheduling is a
   deliberate migration design choice and remains subject to timing/WCET
   validation before vehicle authority.
4. The diagnostics thread is event-driven and is not a port of the 20 Hz CLI.
5. Hardware watchdog policy is not ported yet.  Z-010 therefore makes no
   watchdog-liveness claim and retains BMS_OK disabled.

## Not yet eligible for parity claims

- physical ADC/current acquisition;
- ADBMS SPI/isoSPI acquisition and recovery;
- separate ADBMS temperature heartbeat;
- IMD capture;
- fan PWM actuation;
- IWDG feed/block policy;
- CAN transport/on-wire authority/bus-off recovery;
- estimator/SoP thread integration;
- normal BMS_OK assertion supervisor;
- balancing authority.

Those remain fail-low/non-authoritative until their own migration and target
validation gates are complete.
