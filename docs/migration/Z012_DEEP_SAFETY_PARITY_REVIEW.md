# Z-012 deep safety parity review

Oracle: `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`, firmware 0.5.30.

This review compares the current Zephyr migration through Z-012 against the
actual safety architecture and implementation methods in the frozen FreeRTOS
oracle. It is intentionally broader than fan PWM alone.

## Conclusion

Z-012 is suitable for the next **target-build / contract test** with authority
locked out. It is not yet a vehicle-authority-equivalent replacement for the
FreeRTOS image because several later migration stages are deliberately absent:
IWDG policy, retained panic/fault history, complete BMS readiness/authority,
and the real current/ADBMS/CAN/temperature/IMD actors.

At the current stage the output-authority posture is stricter than the operating
FreeRTOS image: BMS_OK assertion and balancing authority are compile-time
forbidden, PE0 is forced low before normal application startup, and every
Zephyr fatal path reaches the direct fail-low primitive before halt.

## Findings corrected by this review

### 1. Startup heartbeat boundary

FreeRTOS uses:

`startup_grace = elapsed < 3000 ms`

Therefore an unseen heartbeat is stale at **exactly 3000 ms**. The prior Zephyr
runtime used `>` and diagnostic `<=`, extending the grace by one tick at the
boundary.

Corrected Z-012 uses:

- unseen stale: `startup_age_ms >= startup_grace_ms`;
- diagnostic grace-active: `heartbeat_age_ms < startup_grace_ms`.

Seen heartbeat expiry remains `age > timeout`, matching v2.6.27.

### 2. Safety-supervisor overrun release semantics

The v2.6.27 error task records its iteration entry and calls
`osDelayUntil(entry + 50 ms)`. If it has already overrun, it retries immediately
and the following cycle reanchors from the next actual entry.

The earlier Zephyr supervisor skipped missed absolute releases, which could add
another full 50 ms delay after an overrun. The safety supervisor now follows the
FreeRTOS reanchor behavior. The real fan worker already uses the same parity
rule at 200 ms.

Placeholder-only workers retain absolute deadline scheduling until their real
FreeRTOS task bodies are migrated. That remaining difference is explicit and is
not treated as parity for those placeholders.

### 3. Heartbeat counter saturation

FreeRTOS heartbeat counters saturate at `UINT32_MAX`. The earlier Zephyr runtime
used `atomic_inc()`, allowing eventual wrap to zero, where zero means "never
completed".

Z-012 now increments heartbeat sequence numbers through a saturating atomic CAS
helper. This removes a long-uptime false-unseen state.

### 4. Direct fail-low completion barrier

The FreeRTOS direct safety primitive terminates with both data and instruction
synchronization barriers. The Zephyr primitive previously had only `__DSB()`.
It now uses `__DSB(); __ISB();` after the final PE0 reset write.

### 5. RTOS integrity settings promoted to compile-time invariants

The Zephyr configuration already selected assertions, ARM MPU stack guards and
zero application heap, but those were configuration preferences rather than
safety-source invariants.

`ams_safety.c` now refuses compilation unless all are true:

- `CONFIG_ASSERT=y`;
- `CONFIG_ARM_MPU=y`;
- `CONFIG_HW_STACK_PROTECTION=y`;
- `CONFIG_HEAP_MEM_POOL_SIZE=0`.

This preserves the intent of the v2.6.27 `configASSERT` and
`configCHECK_FOR_STACK_OVERFLOW=2` safety mechanisms while using the stronger
hardware MPU stack guard available in Zephyr.

### 6. Heartbeat startup epoch placement

v2.6.27 initializes its heartbeat monitor before constructing safety-critical
RTOS objects/tasks, so object-construction time consumes part of the same
startup grace. Z-012 now records the runtime heartbeat epoch before
`create_thread()` rather than after all thread objects have been constructed.

## Safety-method comparison

| Safety property | FreeRTOS v2.6.27 | Zephyr through Z-012 | Status |
|---|---|---|---|
| BMS_OK startup state | direct PE0 low immediately after HAL init | direct PE0 low at PRE_KERNEL_1 and again in main | at least equivalent |
| BMS_OK normal assertion | error/safety task sole owner | compile-time impossible | stricter for migration |
| fatal fail-low | `ams_safety_panic()` forces low first | custom fatal hook forces low first | equivalent safety action |
| fatal write barriers | DSB + ISB | DSB + ISB | matched |
| balancing authority | operating-profile dependent | compile-time impossible | stricter for migration |
| assertions | FreeRTOS `configASSERT` | Zephyr assertions required | matched intent |
| stack overflow | FreeRTOS canary/check hook | hardware MPU stack guard required | at least equivalent mechanism |
| application heap dependence | safety locks/tasks static; heap still exists elsewhere | application heap size 0 | stricter for current scope |
| priority safety order | safety > current > ADBMS > CAN > estimator > fan/AIR > IMD | same relative order | matched |
| task stack lower bounds | frozen allocations | all Zephyr stacks >= oracle byte equivalents | conservative |
| heartbeat startup grace | 3000 ms, strict `<` | exact strict boundary | matched |
| heartbeat expiry | age `> timeout` | age `> timeout` | matched |
| heartbeat counters | saturating | saturating atomic | matched |
| safety overrun scheduling | reanchor at task entry | reanchor at task entry | matched |
| fan overrun scheduling | reanchor at task entry | reanchor at task entry | matched |
| watchdog | software-liveness IWDG | not yet ported | open later gate |
| retained panic record | yes | not yet ported | open later gate |
| fault/reset history | yes | not yet ported | open later gate |
| current task | real 20 ms safety producer | placeholder; ADC adapter only | intentionally deferred |
| ADBMS/temp | real acquisition + temperature heartbeat | placeholder | intentionally deferred |
| CAN | real protected scheduler/bus-off path | placeholder/disabled | intentionally deferred |
| IMD | profile-gated real capture | disabled placeholder | intentionally deferred |

## Fan safety parity through Z-012

The exact v2.6.27 thermal policy is ported and differentially validated. Missing
or untrusted temperature commands maximum cooling rather than inventing a
nominal temperature. Six PWM outputs use the original TIM3/TIM4/TIM5 mapping,
active-high polarity, 108 MHz timer clock, `PSC=0`, effective `ARR=3360`, and the
legacy CCR conversion including the original 100-percent endpoint.

A missing/unready PWM timer is treated as platform initialization failure and
reaches the existing fail-low panic path. Individual channel command failures
remain process/output faults: all six zones are attempted and the fan heartbeat
is published only after the complete six-zone actuation attempt. The heartbeat
therefore proves fan software execution, not physical airflow; there is no fan
tach feedback in the original hardware.

Z-012 intentionally permits a later Zephyr PWM command to recover after a
transient per-channel command failure. The FreeRTOS `fan_init()` leaves a
channel permanently uninitialized after a startup `HAL_TIM_PWM_Start()` error.
The Zephyr retry behavior is an explicit fail-operational improvement for
cooling, not an undocumented parity claim. A timer/platform failure remains
fatal.

## Current-path state through Z-012

The current-sensor conversion/calibration/range logic and current-fault policy
are exact v2.6.27 behavioral ports. The ADC adapter preserves high-range then
low-range ordering and a bounded 5 ms completion wait. Because Zephyr 4.4 has
no public cancellation for the already-started STM32 ADC transaction, a timeout
latches the adapter unusable for the remainder of the boot so late completion
cannot race reused storage.

The real 20 ms current task, current-window priority-inheritance lock,
publication transaction and immediate BMS fail-low integration remain deferred
to the dedicated integration stage. Until then current runtime heartbeats are
not safety evidence.

## Remaining safety gaps before vehicle authority

The following are release blockers for an authority-capable Zephyr image, but
they do not block Z-012 target-build testing because authority is physically and
compile-time disabled:

1. Port and validate IWDG software-liveness feed/block policy.
2. Port retained panic reason and fault/reset history or establish an equivalent
   diagnostic evidence mechanism.
3. Integrate the real current task and current-window mutex/publication ordering.
4. Integrate ADBMS measurement, temperature heartbeat and recovery semantics.
5. Port IMD capture and validate its stale/fault policy.
6. Port CAN scheduler/on-wire completion and repeated bus-off hardening.
7. Restore the full safety supervisor readiness/state policy before granting
   BMS_OK assertion authority.
8. Validate physical fan startup/pinctrl glitch behavior, frequency, polarity
   and all six outputs on an oscilloscope.

No later stage may use placeholder heartbeat cycles as evidence that an
unmigrated physical safety function is alive.
