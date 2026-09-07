# Z-014 Watchdog / IWDG Migration Plan

**Oracle:** DER26 AMS v2.6.27 / firmware 0.5.30
**Oracle package:** `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`
**Target:** Zephyr 4.4.0, STM32F767ZIT6 / `der26_ams`
**Planning baseline:** completed Z-013 IMD-capture candidate
**Authority during Z-014:** BMS_OK assertion OFF, balancing authority OFF

Z-014 must port the **software-liveness watchdog architecture**, not merely
turn on an STM32 watchdog peripheral. The design is constrained by the exact
v2.6.27 error-task, heartbeat, RTOS-integrity and IWDG behavior reviewed below.

No Z-014 production code is implemented by this document. The purpose of this
stage plan is to freeze the behavior, resolve platform differences before code
is written, and define the evidence required to accept the implementation.

---

## Architecture prerequisite from Z-013 hardening

Z-014 must build on `docs/ARCHITECTURE.md` and
`Z013_ARCHITECTURE_HARDENING.md` rather than reintroducing application/hardware
coupling. In particular:

- application code consumes `include/ams_platform/watchdog.h`, not a
  `watchdog_zephyr.h` implementation header;
- the Zephyr implementation remains `drivers/ams/watchdog_zephyr.c` inside the
  dedicated `ams_platform` Zephyr library;
- `lib/ams_core/watchdog` owns feed policy and has no Zephyr dependency;
- the hardware adapter consumes the native Zephyr watchdog device from
  Devicetree (prefer a board alias such as `ams-watchdog = &iwdg` when one fixed
  application identity is useful); do not create `/zephyr,user` hardware
  properties;
- adapter presence, hardware-active state, full-oracle heartbeat coverage and
  physical target validation remain separate capability/evidence facts;
- `check_all_contracts.py` remains the canonical target gate.

The watchdog stage should strengthen this architecture rather than becoming an
exception to it.

## 1. Reviewed FreeRTOS oracle source

The Z-014 review covered the complete application-level path that can affect
watchdog feed, software liveness, fatal handling or reset evidence, not only
`ams_safety.c`.

| Oracle file | SHA-256 |
| --- | --- |
| `Core/Inc/app.h` | `90071c2ab9fef85fac74e89140c70fb9e59aa578c28d2d8c8058c9d98dc6c808` |
| `Core/Src/app.c` | `eadae1c3cc16c1867d0a701b2c39322ce597fafd42aa922d0250e732dea31d7e` |
| `Core/Inc/tasks/error_task.h` | `aa1a7aa44789d1c3b0aed7b706df9eb0d18a7285a590b686ce868d99b96f38e7` |
| `Core/Src/tasks/error_task.c` | `206bf8b3f5d8884d050ef9be1ab0e368a377af566cdc2a60b1b9cab23aefc663` |
| `Core/Inc/ext_drivers/ams_safety.h` | `a8c224084827ce6dd6fde014390a8b211298ddd717a6c7deec96148a3cafce17` |
| `Core/Src/ext_drivers/ams_safety.c` | `a33dd7f9853b9fe1c531e9bf7380369607f78d131dd2380e0ae7ee1fde32aecc` |
| `Core/Inc/ext_drivers/ams_rtos_diag.h` | `b250b7e61e3c000e0e79b4aeba6c2e35665a2183c24903c18bb95db586c73d20` |
| `Core/Src/ext_drivers/ams_rtos_diag.c` | `031f6931fe844b0f494596fd4e01c8e70fa9561c9cb76cd85f3b7a957e16f661` |
| `Core/Inc/ams_build_profile.h` | `879f83bbdfa47ca2abf3302426405a54bf7b78317e3e579f2196be7eb97afe00` |
| `Core/Inc/FreeRTOSConfig.h` | `f6507b442fd461a8721dc5c52aa0dc9d5a929a3f5c7668f7d68cfb0b4c438d39` |
| `Core/Src/main.c` | `ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84` |
| `Core/Src/stm32f7xx_it.c` | `7270e25841008fb721d26419539a27485a62a50fda2a8e073677b23f38486530` |
| `Core/Src/tasks/current_task.c` | `b0f26b71426af014f94fe2803f6654cf9f2bda0a8af68ed1612ff086593ab0f2` |
| `Core/Src/tasks/adbms_task.c` | `fdbe4ff7f8580c448184b24edc040c1e10f5be50945fc4fa7fdb73d7e8f760e6` |
| `Core/Src/tasks/canbus_task.c` | `75c883321c438d00551afb012b0d566dae2c9f3e9e148c7bc09d09eb08b8564e` |
| `Core/Src/tasks/fan_task.c` | `050e15dafbe95433a254f6f994cfb262dc388c32098a0a7849f64044ec782f63` |
| `Core/Src/tasks/imd_task.c` | `c264f5f773d7280e5227925414736e21bc08fd3f660960ec6e37f1ba3bb3e385` |
| `Core/Src/tasks/estimator_task.c` | `66fd4fc84a4f706cdfa515e943991e03ab81759a6eb742729e7a605d58816854` |
| `host_tests/src/ams_host_test_runner.c` | `b923836c648f9a5da4f4a8d539f9905ffa3cdac2a488ca42b7381f1365df4185` |
| `host_tests/Makefile` | `c5c6d71de09949f06a9b73ee24ccf028be6862f87aa38513da7e71e9c0e8cd87` |

This source set is the Z-014 behavioral oracle. Do not substitute an older
watchdog policy from a previous DER26 revision.

---

## 2. FreeRTOS watchdog architecture — exact behavior

### 2.1 What the IWDG means

The v2.6.27 IWDG is a **software-liveness/software-integrity watchdog**.
It is deliberately not a generic "the accumulator has any fault" watchdog.

A healthy supervisor continues feeding through physical/process faults such as:

- voltage faults;
- temperature faults;
- current/process faults;
- charger faults;
- CAN process faults including a latched bus-off policy fault;
- ADBMS process/diagnostic faults;
- fuse/process faults;
- IMD process faults;
- fan output/process faults.

Those faults independently force BMS_OK low. Resetting the MCU cannot repair
most of them and can destroy useful diagnostic continuity, so they do **not**
starve the IWDG merely because they are faults.

Feed stops for software-integrity/liveness failures:

- panic/fatal condition;
- safety-critical heartbeat stale;
- critical RTOS stack margin;
- RTOS integrity fault (overflow, allocation failure, assert path);
- explicit watchdog stop-feed fault injection;
- watchdog start failure prevents a feed from being reported.

This separation is a hard Z-014 invariant.

### 2.2 Heartbeat sources and final safety mask

The oracle heartbeat IDs are:

1. ADBMS
2. CURRENT
3. TEMP
4. CAN
5. LOGGER
6. IMD
7. FAN
8. ESTIMATOR

The final safety mask is exactly:

- ADBMS;
- CURRENT;
- TEMP;
- CAN;
- FAN;
- IMD when IMD is enabled;
- ESTIMATOR only when SoP authority is required.

LOGGER is deliberately outside the safety mask. A stale logger is diagnostic /
soft-fault evidence and does not stop IWDG feed.

### 2.3 Heartbeat timing

Exact source constants:

| Source | Timeout |
| --- | ---: |
| ADBMS | 3000 ms |
| CURRENT | 200 ms |
| TEMP | 3000 ms |
| CAN | 2000 ms |
| LOGGER | 2000 ms |
| IMD | 500 ms |
| FAN | 1000 ms |
| ESTIMATOR | 500 ms |

Global startup grace: **3000 ms**.

Boundary semantics are exact:

- startup grace active when `elapsed < 3000 ms`;
- an unseen required heartbeat is stale at **exactly 3000 ms**;
- a seen heartbeat remains fresh at `age == timeout`;
- it is stale only when `age > timeout`;
- elapsed calculations use unsigned 32-bit subtraction for tick-wrap safety;
- heartbeat counters saturate at `UINT32_MAX` rather than wrapping to zero.

Z-012/Z-013 already freeze these boundary semantics and must not regress.

### 2.4 Heartbeat kick placement

A kick means the task completed the meaningful work of the cycle, not merely
that the scheduler dispatched it.

Examples from the oracle:

- CURRENT kicks after ADC acquisition/conversion, current-fault update,
  current-window publication, mutex release and immediate fail-low decision;
- FAN kicks after all fan outputs have been attempted;
- IMD kicks after capture evaluation, coherent publication and immediate
  fail-low decision;
- ESTIMATOR kicks after estimator update;
- CAN kicks after the task's CAN work/recovery/authority processing;
- LOGGER kicks only when its detail publication succeeds;
- ADBMS and TEMP are separately kicked at their respective completed evidence
  points.

No later Zephyr stage may count a placeholder loop as physical safety evidence.

### 2.5 Safety-supervisor ownership of feed

The FreeRTOS error/safety task:

- runs at 20 Hz / 50 ms;
- is the highest-priority application task;
- updates heartbeat stale state;
- updates RTOS diagnostics;
- computes the BMS safety decision;
- leaves the safety critical section;
- **then** invokes `ams_safety_watchdog_task_update()`.

There is no independent watchdog feeder task, ISR, timer callback or workqueue.
If the safety supervisor itself dies, feeding stops naturally.

Z-014 must preserve this ownership.

### 2.6 Startup ordering

FreeRTOS startup is materially important:

1. `HAL_Init()`;
2. direct BMS_OK fail-low;
3. reset-cause capture;
4. CPU fault enable;
5. clock/peripheral initialization;
6. kernel initialize;
7. `board_init()` and application initialization;
8. `ams_heartbeat_init()` establishes the 3000 ms epoch;
9. `ams_safety_watchdog_boot_arm()` starts/feeds IWDG if compiled enabled;
10. static mutex/task construction;
11. scheduler starts.

Therefore task construction consumes the same startup-grace interval. Z-013
already records the runtime epoch before thread creation. Z-014 must arm the
watchdog **after that epoch is established and before application thread
creation**, after the real platform adapters that correspond to `board_init()`
have completed.

### 2.7 5000 ms STM32 IWDG configuration

Oracle timeout: **5000 ms nominal**.

The source manually programs STM32 IWDG using nominal 32 kHz LSI:

- prescaler `/64`;
- reload value `2499`;
- bounded wait for PVU/RVU update completion;
- then IWDG start key;
- then immediate feed key.

The actual physical timeout depends on LSI tolerance and must be measured on the
target. Z-014 must not silently tighten or lengthen this policy merely because a
new RTOS API makes another value convenient.

### 2.8 Startup grace is fed

A subtle but important source behavior:

- `ams_safety_watchdog_ok()` reports false during startup grace;
- `ams_safety_watchdog_task_update()` nevertheless **feeds** during startup
  grace.

Startup grace is therefore a controlled initialization state, not watchdog
starvation.

### 2.9 Start failure and retry behavior

The FreeRTOS implementation attempts to start the watchdog during boot. If the
start handshake fails, hardware-started remains false. While the watchdog gate
is enabled, later healthy/startup-grace supervisor cycles retry start.

The host oracle explicitly tests:

1. start failure -> no feed, `START_FAILED`, fail closed;
2. the injected start failure is removed;
3. a later supervisor update starts the watchdog and feeds normally.

This is different from a panic. The source does **not** call
`ams_safety_panic()` merely because the manual IWDG start handshake returned
false. BMS readiness is inhibited when IWDG is compile-enabled and not started.

The Zephyr platform adapter must preserve retry where it is safe to prove that
hardware has not started. If the Zephyr driver produces an ambiguous state in
which hardware may already be running, Z-014 must be more conservative instead
of pretending source-identical retry semantics.

### 2.10 Runtime disable is intentionally irreversible

Once the hardware watchdog has started, a software request to disable the
watchdog is ignored. Stopping feed after an irreversible start would merely
schedule an unexpected reset.

Z-014 must not call `wdt_disable()` as a normal recovery operation.

### 2.11 Block-reason telemetry

The source block-reason values are stable diagnostic schema:

- NONE
- NOT_ENABLED
- PANIC
- STARTUP_GRACE
- HEARTBEAT
- ADBMS_STALE (legacy retained value)
- CURRENT_STALE (legacy retained value)
- TEMP_STALE (legacy retained value)
- HARD_FAULT (legacy retained value)
- STOP_FEED_TEST
- START_FAILED
- RTOS_INTEGRITY

The ADBMS/CURRENT/TEMP/HARD_FAULT entries are retained numeric/log compatibility
values and are no longer selected by the current policy.

Z-014 should preserve their numbering in a portable enum so future telemetry /
retained-log migration does not accidentally remap historical values.

### 2.12 Feed/block counters

- feed count saturates at `UINT32_MAX`;
- block count saturates at `UINT32_MAX`;
- last feed timestamp is updated only on a real feed;
- NOT_ENABLED and STARTUP_GRACE are status states rather than feed-stop fault
  log events;
- a real feed-stop event is logged only when its reason changes.

The retained fault-log transport itself is outside Z-014 scope, but Z-014 must
maintain sufficient runtime state so the same event can be connected later
without changing watchdog policy.

---

## 3. FreeRTOS RTOS-integrity behavior that is part of watchdog parity

Porting only heartbeat stale checks would be incomplete.

### 3.1 Stack-overflow / assert / malloc failure

The oracle configures:

- FreeRTOS stack-overflow checking level 2;
- `configASSERT()` routed to the AMS panic path;
- malloc-failure hook enabled.

Fatal RTOS hooks force BMS_OK low **before** diagnostic bookkeeping. The
existing Zephyr fatal hook already preserves the most important ordering:
physical fail-low first, then halt.

### 3.2 Pre-overflow stack-margin safety gate

The full source review identified an important parity requirement that is not
yet represented by Z-013's periodic runtime policy.

FreeRTOS does not wait for an actual stack overflow before considering software
integrity unsafe. Every 50 ms the safety supervisor checks high-water marks.

Per-task warning threshold:

`max(96 words, ceil(25% of configured stack words))`

Per-task critical threshold:

`max(64 words, ceil(15% of configured stack words))`

On Cortex-M7 a word is 4 bytes. A warning is diagnostic/soft. A critical stack
margin:

- sets `rtos_stack_critical`;
- contributes to hard fault / BMS fail-low;
- stops watchdog feeding with `RTOS_INTEGRITY`.

Zephyr through Z-013 has MPU hardware stack protection, which is a strong
actual-overflow mechanism, but it does **not** by itself reproduce this
pre-overflow headroom gate. Z-014 must close that gap before claiming watchdog
parity.

### 3.3 Zephyr stack-margin rule for Z-014

Use `k_thread_stack_space_get()` on every created application thread from the
50 ms safety-supervisor path.

Apply the same policy in bytes:

- warning = `max(384 bytes, ceil(25% of usable configured stack bytes))`;
- critical = `max(256 bytes, ceil(15% of usable configured stack bytes))`.

Using the larger Zephyr allocations with the same percentages is conservative:
it warns/blocks while more absolute headroom remains than the corresponding
FreeRTOS task.

Rules:

- a disabled/non-created thread is excluded;
- failure to obtain stack-space data for an enabled created thread is treated as
  integrity-unknown and therefore critical/fail-closed for watchdog purposes;
- warning alone does not stop feed;
- critical does stop feed;
- an actual MPU stack violation still goes directly through the fatal fail-low
  path and must not rely on the periodic margin monitor.

The added scan cost must be included in safety-thread WCET measurements.

### 3.4 Heap behavior

The FreeRTOS oracle contains a heap and warns below 2048 bytes. Current Zephyr
migration has `CONFIG_HEAP_MEM_POOL_SIZE=0` as a compile-time invariant.

Do not create a fake heap-warning analog. The zero-application-heap design is
strictly simpler. Any future stage that introduces application heap usage must
reopen this safety decision before authority is permitted.

---

## 4. Critical current Z-013 limitation: placeholders cannot become watchdog evidence

Z-013 correctly records two separate properties in each runtime descriptor:

- `safety_heartbeat_required` — whether the final source policy requires that
  subsystem's liveness;
- `safety_evidence_ready` — whether the migrated worker is real enough to count
  as safety evidence now.

At Z-013:

- FAN: required + evidence ready;
- IMD: required + evidence ready;
- CURRENT: required but placeholder, not evidence ready;
- ADBMS: required but placeholder, not evidence ready;
- CAN: required but placeholder, not evidence ready;
- ESTIMATOR: not currently safety-required and placeholder;
- separate TEMP heartbeat: not yet integrated;
- AIR: no source heartbeat;
- diagnostics: not source LOGGER and not safety evidence.

A Z-014 implementation that feeds based on the placeholder heartbeat sequence
would violate the migration safety rule even if its timing looked correct.

### 4.1 Required staged-mask design

Z-014 must carry **two masks** explicitly:

1. `oracle_required_mask`
   - exact final v2.6.27 safety heartbeat policy;
2. `migration_evidence_mask`
   - only real migrated safety workloads allowed to prove liveness in this
     no-authority stage.

At Z-014 the effective validation mask is:

`oracle_required_mask & migration_evidence_mask`

This is not presented as final vehicle parity. It is a controlled migration
adaptation that prevents fake safety evidence while allowing the real IWDG
hardware/feed mechanism to be validated before CURRENT/ADBMS/TEMP/CAN are
ported.

The build manifest must expose:

- final oracle required mask;
- current evidence-ready mask;
- effective validation mask;
- `watchdog_full_oracle_coverage=false`.

A future authority compile gate must require:

`migration_evidence_mask == oracle_required_mask`

before BMS authority can become possible. Missing TEMP is therefore impossible
to hide.

The safety supervisor itself is implicitly required because it is the sole feed
owner; there is no self-heartbeat bit.

---

## 5. Zephyr platform choice

### 5.1 Use the native Zephyr watchdog API, with an AMS adapter

Preferred Z-014 platform path:

- Zephyr `watchdog` device API;
- STM32 IWDG node enabled in DTS;
- a small policy-free `drivers/ams/watchdog_zephyr.c` implementation behind `include/ams_platform/watchdog.h`;
- portable feed-policy/state machine in `lib/ams_core`.

Do not call Zephyr watchdog APIs directly from the safety policy core.

Expected configuration:

- 5000 ms max window;
- 0 ms min window;
- `WDT_FLAG_RESET_SOC`;
- no callback;
- no normal `wdt_disable()` path.

Zephyr 4.4 requires explicit watchdog configuration/setup; do not rely on a
Kconfig option to "automatically" start the watchdog.

### 5.2 Debugger pause policy

The FreeRTOS source does not program the STM32 DBGMCU IWDG-freeze bit.
Therefore the release/validation parity build should **not** request
`WDT_OPT_PAUSE_HALTED_BY_DBG`.

A separate developer-only debug overlay may later pause the watchdog if needed,
but evidence from that overlay does not satisfy the target watchdog gate.

### 5.3 Important Zephyr setup-failure difference

The STM32 Zephyr IWDG driver must be inspected at the exact local v4.4.0 source
before implementation. The current driver family enables the IWDG as part of
`wdt_setup()`, configures it, waits for the status update, then reloads it.
Consequently, an error returned from `wdt_setup()` can be more ambiguous than
the FreeRTOS manual sequence: hardware may already have become irreversible.

Z-014 must classify platform start state, not store a simple boolean.

Recommended states:

- `UNCONFIGURED`
- `READY_NOT_STARTED`
- `STARTED`
- `START_FAILED_RETRYABLE`
- `START_AMBIGUOUS_TERMINAL`

Rules:

- failures before any call capable of enabling hardware may be retried from the
  safety supervisor, preserving the FreeRTOS retry concept;
- once setup has entered a path that may have enabled IWDG, an error is terminal
  for that boot;
- terminal ambiguous state forces BMS low and never reports a successful feed;
- do not attempt disable/reconfigure/reuse after an ambiguous start;
- if hardware did start, starvation naturally resets the MCU;
- if it did not, the image remains halted/fail-low instead of pretending health.

This is intentionally more conservative than the oracle at the platform error
boundary while preserving the normal healthy behavior.

### 5.4 Verify generated register result

The target contract must prove that the 5000 ms Zephyr request resolves to the
same effective STM32 configuration as the oracle where practical:

- nominal LSI basis: 32 kHz;
- prescaler: /64;
- reload: 2499.

If Zephyr selects a behaviorally equivalent representation due to driver/API
changes, document and measure it rather than forcing private register writes
without cause.

---

## 6. Proposed Z-014 software architecture

### 6.1 Portable policy core

Add approximately:

- `lib/ams_core/include/ams_core/ams_watchdog_policy.h`
- `lib/ams_core/watchdog/ams_watchdog_policy.c`
- `lib/ams_core/watchdog/ORACLE_PROVENANCE.md`

The core owns:

- block-reason enum with exact legacy numbering;
- startup-grace decision;
- heartbeat stale-mask decision;
- RTOS-integrity decision;
- panic/stop-feed decision;
- feed-vs-block action;
- saturating feed/block counters if counters are kept in the policy object;
- reason-change event indication;
- final-oracle mask and migration evidence mask representation.

The core must have no Zephyr, STM32, HAL, CMSIS or FreeRTOS dependency.

### 6.2 Policy inputs

A single coherent input snapshot should contain at least:

- `now_ms`;
- `boot_ms`;
- runtime-enabled flag;
- platform watchdog state;
- panic/fatal-latched flag where observable in normal context;
- stop-feed-test flag in validation builds;
- oracle safety-stale mask;
- effective migrated safety-stale mask;
- RTOS-integrity fault;
- stack-critical mask;
- full-oracle-coverage flag.

Do **not** feed physical process-fault booleans into this policy merely to ignore
them. Keep the interface narrow so a later developer cannot casually add
"hard_fault means reset" without changing a reviewed contract.

### 6.3 Policy outputs

Return a deterministic action object:

- block reason;
- feed permitted;
- start/retry permitted;
- actual health-good (`watchdog_ok`) state;
- log-on-reason-change flag;
- coverage-complete state.

Startup grace must preserve the source split:

- feed permitted = true;
- health-good = false;
- reason = STARTUP_GRACE.

### 6.4 Zephyr hardware adapter

Add approximately:

- `include/ams_platform/watchdog.h`
- `drivers/ams/watchdog_zephyr.c`

Responsibilities only:

- obtain watchdog device;
- verify readiness;
- install one 5000 ms RESET_SOC timeout;
- setup/start once;
- classify ambiguous setup failures;
- feed by channel ID;
- expose immutable startup/error diagnostics;
- never decide whether the application is healthy;
- never create its own feeder thread/timer/work item.

### 6.5 Runtime integrity monitor

Extend `ams_threads.c/.h` with a safety-owned stack-health snapshot:

- per-thread unused bytes;
- warning mask;
- critical mask;
- snapshot-error mask;
- minimum unused bytes;
- optionally last/worst observed unused bytes.

The 50 ms safety task computes this before the watchdog feed decision.

### 6.6 Safety-supervisor integration order

Each Z-014 safety iteration should be ordered:

1. publish safety-task start timing;
2. update stale flags for migrated heartbeat producers;
3. compute stack/headroom integrity state;
4. derive coherent watchdog input snapshot;
5. perform any future BMS/process supervisor work (still no assertion authority
   in Z-014);
6. execute watchdog policy;
7. if action says feed, call the hardware adapter exactly once;
8. update watchdog runtime diagnostics from the real adapter result;
9. publish safety-task completion timing;
10. delay/reanchor using the already-reviewed FreeRTOS-equivalent 50 ms rule.

The feed should be as late as practical in the supervisor cycle so it proves the
supervisor completed all required integrity work.

If the feed API itself fails after hardware is started:

- never count it as a feed;
- mark platform/integrity failure;
- force BMS low;
- cease further health claims;
- allow the hardware watchdog to reset the system.

### 6.7 Boot integration order

Within `ams_threads_start()`:

1. complete Z-013 IMD platform initialization;
2. initialize watchdog policy state without starting hardware;
3. record `runtime_start_ms` / heartbeat epoch;
4. arm the watchdog hardware;
5. create static application thread objects;
6. set runtime active;
7. start safety supervisor first;
8. start lower-priority workers.

This most closely matches `heartbeat_init -> watchdog_boot_arm -> task creation`
in v2.6.27.

Any thread-creation/startup panic after IWDG arm causes direct BMS fail-low and
then no further feeding, allowing IWDG reset in a watchdog-validation build.

---

## 7. Kconfig / DTS / authority design

### 7.1 Board DTS

Use the native STM32 IWDG Devicetree node and Zephyr watchdog binding.  If the
application needs a stable board-level identity, add a standard alias such as
`ams-watchdog = &iwdg` and have the platform adapter consume
`DT_ALIAS(ams_watchdog)`.  Do not add an untyped `/zephyr,user` watchdog
property or duplicate the native watchdog binding.

Enable the IWDG only in the explicit Z-014 validation configuration. Do not
change unrelated clock/peripheral nodes.

### 7.2 Kconfig / capability concepts

Continue the Z-013 capability model instead of adding a second source of truth.
At Z-014 the relevant facts are:

- `CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT=y` only when the production Zephyr
  adapter is compiled and contract-tested;
- `CONFIG_AMS_CAP_WATCHDOG_ACTIVE=y` only in the no-authority validation image
  that actually arms IWDG;
- `CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE=n` until real CURRENT, ADBMS,
  TEMP, CAN and required ESTIMATOR evidence are all migrated;
- add a user-facing `CONFIG_AMS_IWDG_TARGET_VALIDATED=n` release-evidence gate,
  which may become true only after physical starvation/reset validation;
- existing BMS/balance authority remain `n`.

Capability symbols remain hidden migration facts.  Target-validation evidence
is not an authority control and does not imply full heartbeat coverage.

Compile-time assertions must reject any Z-014 validation build that can assert
BMS_OK or balancing authority.

### 7.3 Full-coverage gate

Add a compile/build contract equivalent to:

`CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE=n` at Z-014.

It may become true only after real CURRENT, ADBMS, TEMP, CAN and any required
ESTIMATOR producer is migrated and its heartbeat is approved as safety
evidence.

No future authority gate may accept watchdog target validation alone without
full oracle coverage.

---

## 8. Z-014 host/SIL test plan

### 8.1 Exact portable-policy differential tests

Build a small FreeRTOS-oracle adapter around the exact v2.6.27 watchdog policy
and compare it against the portable Z-014 core.

Exercise at least:

- startup grace `2999`, `3000`, `3001` ms;
- every heartbeat timeout at `T-1`, `T`, `T+1`;
- unseen heartbeat at grace boundary;
- 32-bit tick wrap;
- each safety stale bit independently;
- logger stale independently;
- estimator-required true/false;
- IMD enabled/disabled;
- panic;
- RTOS fault;
- stack critical;
- stop-feed test;
- not enabled;
- start failed;
- external process faults while liveness remains good;
- feed/block counter saturation;
- block-reason transition logging;
- repeated identical block reason;
- recovery from stale heartbeat;
- recovery from retryable pre-start failure.

Target differential campaign: minimum **2.5 million stateful decisions** across
five deterministic seeds and `-O0/-O1/-O2/-O3/-Os`, following Z-009/Z-011/Z-013
practice.

### 8.2 Directed policy tests

Freeze explicit checks for the source's counterintuitive rules:

- startup grace feeds;
- process hard faults do not stop feed;
- logger stale does not stop feed;
- safety heartbeat stale does stop feed;
- stack warning does not stop feed;
- stack critical does stop feed;
- panic does stop feed;
- disabled watchdog does not increment fault-stop count;
- start failure cannot increment feed count.

### 8.3 Production Zephyr watchdog-adapter SIL

Compile the actual `watchdog_zephyr.c` against a fake Zephyr watchdog device.
Cover:

- device not ready;
- timeout install failure;
- correct 5000 ms config/RESET_SOC flag;
- channel ID handling;
- setup success;
- setup failure / ambiguous-state latch;
- feed success/failure;
- no feed before start;
- idempotent start API;
- no disable/restart after irreversible/ambiguous state;
- no callback configured;
- debugger-pause option absent in validation/release path.

Randomized adapter state-machine stress should be at least 100,000 operations.

### 8.4 Stack-integrity tests

Compile the actual threshold helper and verify:

- 25% warning / 15% critical percentages;
- 384 B / 256 B floors;
- exact threshold equality semantics;
- every active thread represented;
- disabled thread excluded;
- stack-query failure -> critical unknown;
- warning-only permits feed;
- critical blocks feed;
- masks/counters do not wrap incorrectly.

### 8.5 Concurrency/stress tests

Stress supervisor snapshots while worker heartbeats update concurrently.
Required invariants:

- no lost heartbeat due to read/modify/write races;
- no torn mask that can transiently authorize a feed;
- a new stale/integrity fault discovered before the feed point prevents that
  cycle's feed;
- a post-feed fault cannot make the previous feed retroactively unsafe, but is
  observed by the next 50 ms supervisor cycle;
- stop-feed test cannot be cleared by unrelated state publication;
- no placeholder is accepted as `safety_evidence_ready`.

Use TSAN where the fake host surface makes it practical; otherwise use seeded
threaded stress plus atomic/memory-order contract review.

### 8.6 Static analysis/sanitizers

Mandatory host evidence:

- `-Wall -Wextra -Werror`;
- GCC `-fanalyzer` on policy + production adapter;
- Clang static analyzer on policy + production adapter;
- ASan + UBSan on directed/randomized host suites;
- source-dependency scan proving portable policy has no Zephyr/HAL/FreeRTOS
  dependency;
- Python contract scripts compile cleanly.

---

## 9. Z-014 target-build contract

Add `scripts/check_watchdog_contract.py` and extend existing runtime/safety
contracts.

The generated build must prove at least:

- STM32F767 / DER26 board unchanged;
- IWDG node exists and is enabled only in the intended validation config;
- Zephyr watchdog driver and AMS watchdog adapter are linked;
- timeout constant is 5000 ms;
- RESET_SOC behavior requested;
- no watchdog callback feeder exists;
- no independent watchdog thread/workqueue/timer exists;
- safety supervisor remains sole feed caller;
- feed occurs after stale + stack-integrity evaluation in source order;
- direct fatal fail-low path remains first;
- BMS authority remains off;
- balancing authority remains off;
- application heap remains 0;
- IMD target validation remains independent;
- `watchdog_full_oracle_coverage=false` at Z-014;
- CURRENT/ADBMS/CAN placeholders remain `safety_evidence_ready=false`;
- TEMP heartbeat remains explicitly not integrated;
- the build manifest records watchdog port/validation/coverage state.

`check_freertos_runtime_parity.py` must be updated to distinguish:

- exact final oracle heartbeat mask;
- current migrated evidence mask;
- Z-014 hardware/policy parity;
- remaining full-coverage deferrals.

Do not make the script print generic "watchdog parity PASS" if full oracle
coverage is still false.

---

## 10. Z-014 target hardware validation

Target build success is not watchdog validation.

### 10.1 Healthy-feed test

With BMS/balance authority physically inhibited:

- boot the Z-014 watchdog-validation image;
- confirm no reset during healthy runtime;
- confirm feed cadence from diagnostics/instrumentation;
- verify safety-thread WCET and feed margin;
- run longer than several watchdog periods.

### 10.2 Deliberate starvation/reset test

Use a compile-time or tightly controlled validation-only stop-feed injection.

Prove:

1. BMS_OK is already physically low;
2. feed stops for the requested software-integrity test reason;
3. no later context resumes feeding;
4. MCU resets by IWDG;
5. measured starvation-to-reset time matches the accepted 5000 ms nominal
   configuration and LSI tolerance;
6. reset cause contains the STM32 IWDG reset indication.

Do this without relying on a debugger halt if the release-equivalent build does
not freeze IWDG under debug.

### 10.3 Safety-supervisor death test

Prove there is no independent feeder:

- deliberately prevent the safety supervisor from progressing after IWDG is
  armed;
- all lower-priority workers may remain alive;
- IWDG must still expire/reset because only the supervisor owns feed.

### 10.4 Required-heartbeat starvation tests

At Z-014 hardware-validation coverage, starve each **real evidence-ready**
source individually (currently fan and IMD) and prove feed stops after the exact
source timeout boundary plus at most one safety-supervisor scheduling interval.

Do not claim CURRENT/ADBMS/TEMP/CAN heartbeat hardware validation until those
real workloads exist.

### 10.5 Process-fault non-reset tests

Inject/produce a bad IMD process state while the IMD software task continues to
run. BMS_OK must remain low, but IWDG must continue feeding because software
liveness is healthy.

Likewise exercise fan output/process fault where practical. This test is as
important as the starvation test because it prevents accidental conversion of
the IWDG into a process-fault reset loop.

### 10.6 Stack-margin validation

Create a controlled test build with a known stack consumer or reduced test
stack and demonstrate:

- warning threshold is reported without stopping feed;
- critical threshold stops feed/fails low before actual MPU overflow;
- actual guard violation enters the fatal fail-low path.

### 10.7 Reset evidence boundary

Z-014 needs enough reset-cause observation to verify an IWDG reset occurred.
However, do **not** claim full parity for the oracle's `.noinit` panic record and
32-entry CRC/commit fault log unless those mechanisms are actually ported and
validated.

Full retained panic/fault history should be a separate later stage or explicit
Z-014B extension.

---

## 11. Acceptance timing / margin rules

The 5 s watchdog timeout is intentionally much longer than the 50 ms safety
period. Acceptance must still measure margin rather than assume it.

Record:

- safety task period and max observed start lateness;
- safety task WCET including stack scan;
- max interval between successful watchdog feeds;
- physical IWDG starvation-to-reset time;
- longest interrupt-disabled section during the test;
- CPU utilization under normal and diagnostic/fault load.

The normal maximum feed interval must remain comfortably below the measured
minimum watchdog-reset interval. Any unexplained long-tail feed interval blocks
Z-014 acceptance.

---

## 12. Regression matrix against existing migration stages

Z-014 may not regress:

- Z-003 direct PE0 fail-low and fatal ordering;
- compile-time no-authority barriers;
- Z-007 coherent measurement store;
- Z-008 current-window semantics;
- Z-009 estimator numerical parity;
- Z-010 power/SoH/fuse core;
- Z-011 current-sensor/fault and bounded ADC adapter;
- Z-012 fan policy/PWM/fail-operational channel behavior;
- Z-013 IMD capture, fail-low-before-heartbeat ordering and target-validation
  separation.

Do not rerun every historically expensive campaign blindly after a watchdog-only
edit, but run all contract gates and the relevant fast regression tests. Re-run
expensive algorithm campaigns only if production algorithm source or shared
compiler/build configuration changes.

---

## 13. Proposed implementation sequence

Implement Z-014 in this order:

1. freeze oracle hashes and this plan;
2. add portable watchdog enum/policy with host directed tests;
3. differential-test portable policy against exact v2.6.27 behavior;
4. add stack-margin policy/helpers and tests;
5. inspect exact local Zephyr 4.4 STM32 IWDG driver and binding;
6. add policy-free Zephyr watchdog adapter;
7. production-adapter fake-Zephyr SIL + analyzers/sanitizers;
8. add DTS/Kconfig validation mode with authority still impossible;
9. arm watchdog after runtime epoch and before thread creation;
10. integrate feed at end of the safety-supervisor integrity path;
11. add diagnostics/build-manifest fields;
12. add `check_watchdog_contract.py` and update runtime/safety parity scripts;
13. run host/SIL and source analysis;
14. run STM32 target build + full contract set;
15. run target healthy-feed/starvation/supervisor-death/process-fault tests;
16. only then set the **watchdog target-validation evidence flag** for the
    no-authority migration program;
17. keep full-oracle-coverage and BMS authority false until later subsystem
    stages converge the heartbeat evidence mask.

---

## 14. Expected Z-014 file scope

Likely new files:

- `lib/ams_core/include/ams_core/ams_watchdog_policy.h`
- `lib/ams_core/watchdog/ams_watchdog_policy.c`
- `lib/ams_core/watchdog/ORACLE_PROVENANCE.md`
- `include/ams_platform/watchdog.h`
- `drivers/ams/watchdog_zephyr.c`
- `tests/unit/watchdog/...`
- `scripts/check_watchdog_contract.py`
- `docs/migration/Z014_WATCHDOG_IWDG_PARITY.md`

Likely modified files:

- `lib/ams_core/CMakeLists.txt`
- `drivers/ams/CMakeLists.txt`
- `app/Kconfig`
- `app/prj.conf` or a dedicated validation overlay;
- `app/src/main.c` only if platform initialization ownership requires it;
- `app/src/ams_threads.c/.h` for stack integrity, startup arm and feed;
- `boards/drexel/der26_ams/der26_ams.dts` for IWDG node status/alias;
- `scripts/check_runtime_contract.py`;
- `scripts/check_freertos_runtime_parity.py`;
- `scripts/check_safety_integrity_contract.py`;
- `scripts/check_capability_contract.py`;
- `scripts/check_architecture_contract.py`;
- `scripts/check_all_contracts.py`;
- `scripts/build_manifest.py`;
- core/readme migration documentation.

No current/ADBMS/CAN production integration belongs in Z-014.

---

## 15. Z-014 no-go conditions

Stop and resolve before accepting Z-014 if any of the following occurs:

- a feeder exists outside the safety supervisor;
- a placeholder heartbeat can satisfy the watchdog safety evidence mask;
- process faults starve IWDG contrary to the oracle;
- logger stale becomes watchdog-critical;
- startup grace is not fed;
- exact timeout/stale boundary semantics drift;
- stack critical headroom is omitted or treated only as a warning;
- IWDG setup failure can be misreported as "not started" when hardware may be
  running;
- code attempts to disable/restart an already-started IWDG;
- BMS or balancing authority becomes possible;
- a target reset is claimed without measured IWDG reset evidence;
- retained panic/fault-log parity is claimed without actually porting it;
- the target watchdog build depends on debug-freeze behavior not present in the
  release-equivalent configuration.

---

## 16. Definition of done for Z-014

Z-014 is complete only when all are true:

- portable watchdog policy matches the reviewed v2.6.27 semantics;
- exact heartbeat/grace boundaries are preserved;
- process-fault vs software-integrity separation is proven;
- pre-overflow stack critical behavior is restored in Zephyr;
- native STM32 IWDG adapter uses the 5000 ms source policy;
- setup ambiguity is handled fail-closed;
- safety supervisor is the sole feed owner;
- placeholders are excluded from safety evidence;
- host directed/randomized/differential tests pass;
- sanitizer/static-analysis gates pass;
- STM32F767 target build and generated contracts pass;
- BMS_OK/balance authority remain disabled;
- real hardware healthy-feed and deliberate-starvation reset tests pass;
- IWDG reset cause is observed;
- build manifest truthfully reports **partial migration heartbeat coverage**;
- `AMS_IWDG_TARGET_VALIDATED` is not used as a proxy for full vehicle-watchdog
  coverage;
- no full retained fault-history claim is made yet.

At that point the watchdog peripheral and feed policy are migrated and target
validated for the no-authority Zephyr program. Vehicle-authority watchdog parity
remains gated on the later real CURRENT/ADBMS/TEMP/CAN/ESTIMATOR coverage.
