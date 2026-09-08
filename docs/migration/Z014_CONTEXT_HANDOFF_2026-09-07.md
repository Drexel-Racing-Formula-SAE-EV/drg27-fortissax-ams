# DER27 AMS Zephyr Migration — Z-014 Context / Handoff

**Date:** 2026-09-07
**Current worktree:** `z014_work`
**Target:** STM32F767ZIT6 / custom Zephyr board `der26_ams`
**Zephyr:** 4.4.0
**Behavioral oracle:** DER26 AMS v2.6.27 / FW 0.5.30
**Oracle package:** `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`

## 1. Critical handoff state

This repository is a **mid-Z-014 working tree**, packaged intentionally as-is because the chat/session is ending. Do not treat it as a completed Z-014 release candidate.

The last fully target-linked architecture-hardened Z-013 image built successfully on the user's Windows west workspace:

- target build: **187/187 linked**;
- FLASH: **159,488 B / 2 MiB (7.60%)**;
- RAM: **46,976 B / 384 KiB (11.95%)**;
- architecture, board, no-authority, runtime, portable core, measurement, current-window, estimator, power, current sensor, current ADC, and fan contracts passed;
- the unified contract run then stopped at an IMD contract hash/packaging defect. That checker was subsequently corrected in the next full-repo package, but the user did not paste a final post-fix `check_all_contracts.py` completion before Z-014 work began.

Therefore: **do not rewrite or re-prove Z-001..Z-013 from scratch, but do not falsely claim a final all-contract Z-013 transcript either.** The target link itself was green. Z-014 now changes production/runtime/CMake/Kconfig/DTS again and needs its own fresh target build and complete contract run.

## 2. Migration design goal

The goal is not a Zephyr-shaped FreeRTOS port. The architecture boundary is intentional:

```text
app/                       orchestration, task ownership, safety sequencing
  |
  +--> lib/ams_core/       deterministic portable behavior/policy
  |      NO Zephyr / FreeRTOS / CMSIS / STM32 HAL / board dependencies
  |
  +--> include/ams_platform/   platform-neutral hardware contracts
             |
             +--> drivers/ams/ Zephyr implementations
                        |
                        +--> native Zephyr driver APIs + Devicetree
                                   |
                                   +--> STM32F767 hardware
```

The one intentional exception is emergency BMS fail-low:

```text
fatal/integrity failure
  -> ams_platform/fail_low.h
  -> board-specific direct fail-low primitive
  -> direct PE0 low
```

That path must remain independent of scheduler health and ordinary GPIO-driver progress.

### Mechanical architecture rule

`lib/ams_core/` must compile and test on a host with **no RTOS headers available**. `scripts/check_null_platform_core.py` and native CI mechanically enforce this. If core code needs `k_mutex`, `k_thread`, `osDelay`, HAL, a board header, or `ams_platform`, the abstraction boundary is wrong.

This rule becomes especially important later for ADBMS and CAN:

- ADBMS portable side: commands, PEC, parsing, chain ordering, cell mapping, protocol state that is independent of transport timing.
- ADBMS platform side: SPI transaction mechanism, CS timing, isoSPI wake/session timing, interrupt/preemption-sensitive transport behavior.
- CAN portable side: frame encoding, scheduler policy, counters/staleness, bus-off event-window policy.
- CAN platform side: Zephyr CAN operations, physical controller state, ISR/error identity, TX completion/on-wire evidence.

Do not move transport timing into `ams_core`; do not strand deterministic safety policy inside a Zephyr driver.

## 3. Z-gate history / current status

### Z-001 — Workspace/bootstrap — COMPLETE
Custom Zephyr workspace/application foundation established.

### Z-002 — DER26 board / Devicetree — COMPLETE
Custom `der26_ams` STM32F767 board definition and hardware mapping established.

### Z-003 — direct BMS fail-low + no-authority foundation — COMPLETE
BMS_OK assertion and balancing authority compile-time disabled. Direct fail-low exists as a board/platform safety escape hatch.

### Z-004 / Z-005 — static runtime, priorities, stacks, timing, liveness — COMPLETE
Static thread model, stack ownership, task timing and heartbeat contracts established. Safety supervisor remains highest-priority application supervisor.

### Z-006 — portable common core — COMPLETE
Portable deterministic core established and later architecture-hardened with true null-platform compilation.

### Z-007 — coherent measurement store — COMPLETE
Coherent measurement publication/snapshot semantics ported and host-tested.

### Z-008 — current-window core — COMPLETE
Exact v2.6.27 current-window behavior ported, including earlier race/cross-boundary fixes from the oracle.

### Z-009 — estimator / EKF core — COMPLETE
Exact v2.6.27 estimator behavior ported and host-tested.

### Z-010 — SoP / SoH / fuse observer + runtime safety parity — COMPLETE
Portable power-estimation/observer behavior and runtime safety parity established.

### Z-011 — current sensor/fault + Zephyr ADC adapter — COMPLETE IN CODE; physical evidence remains separate
Portable exact current sensor/fault policy plus bounded Zephyr ADC adapter. Important adaptation: asynchronous ADC read + bounded poll prevents indefinite blocking; on timeout adapter fails closed and does not reuse storage that a late ISR could write.

### Z-012 — fan policy + six-zone Zephyr PWM — COMPLETE IN CODE; physical waveform validation separate
Exact v2.6.27 thermal policy and six-zone mapping. Failure classification and retry behavior are explicit. Fan heartbeat proves software actuation attempts, not physical airflow.

### Z-013 — IMD capture + architecture hardening — TARGET LINK GREEN; final unified post-packaging transcript not captured
IMD core and Zephyr capture adapter implemented. Architecture hardening added typed AMS hardware contracts, `ams_platform` Zephyr module ownership, null-platform CI, capability/evidence separation, and board-specific direct fail-low ownership. Target link was green at 187/187. Final unified contract run was interrupted by checker packaging issues that were fixed afterward.

### Z-014 — watchdog / IWDG + proactive stack integrity — **IN PROGRESS IN THIS TREE**
Do not call complete yet. See Sections 4–8.

Latest target evidence (2026-09-07): the real Zephyr 4.4 `der26_ams/stm32f767xx` base image links successfully (FLASH 165,264 B, RAM 47,424 B), and the user's most recent unified run passed every target-specific contract through `check_watchdog_contract.py`. The run then stopped in the source-only null-platform checker because Windows selected the Visual Studio multi-config generator and the checker omitted `-C Release` for CTest. That checker is now fixed to select Release for multi-config generators and to use CMake File API compile metadata when `compile_commands.json` is unavailable. This does **not** make Z-014 green: the unified command must be rerun to completion, the watchdog-active image must build, and the physical IWDG gate remains mandatory.

## 4. Z-014 safety semantics that are frozen

Z-014 ports the **software-liveness/software-integrity watchdog architecture**, not merely an IWDG peripheral.

### Process faults are NOT watchdog starvation by themselves
Voltage, temperature, current/process, charger, CAN process/bus-off policy, ADBMS diagnostic, fuse, IMD state and fan-output faults may force BMS_OK low while the supervisor continues feeding if software remains healthy. Rebooting cannot repair most physical faults and must not create reset loops.

### Feed must stop for software integrity/liveness failures
- panic/fatal condition;
- required safety heartbeat stale;
- critical proactive stack margin;
- RTOS integrity fault / stack-query uncertainty;
- explicit validation-only stop-feed injection;
- unrecoverable watchdog start/feed platform failure.

### Exact heartbeat oracle
IDs: ADBMS, CURRENT, TEMP, CAN, LOGGER, IMD, FAN, ESTIMATOR.

Timeouts:
- ADBMS 3000 ms
- CURRENT 200 ms
- TEMP 3000 ms
- CAN 2000 ms
- LOGGER 2000 ms (diagnostic; not in safety mask)
- IMD 500 ms
- FAN 1000 ms
- ESTIMATOR 500 ms when SoP authority requires it

Startup grace: **3000 ms**.

Boundary semantics:
- startup grace while elapsed `< 3000`;
- unseen required source stale at exactly `3000`;
- seen source fresh at `age == timeout`;
- stale only at `age > timeout`;
- unsigned 32-bit elapsed math for wrap safety;
- heartbeat/feed/block counters saturate rather than wrap.

Legacy watchdog block-reason numbering must remain stable:
`NONE, NOT_ENABLED, PANIC, STARTUP_GRACE, HEARTBEAT, ADBMS_STALE, CURRENT_STALE, TEMP_STALE, HARD_FAULT, STOP_FEED_TEST, START_FAILED, RTOS_INTEGRITY`.

### Sole feeder
The 20 Hz / 50 ms safety supervisor is the **only** owner allowed to feed. No timer, workqueue, ISR, main loop, or independent watchdog thread may feed. Killing/blocking the safety supervisor must naturally lead to reset even if worker threads remain alive.

### Startup grace nuance
During startup grace the oracle feeds the watchdog, but watchdog health is not yet declared good:

```text
block/status reason = STARTUP_GRACE
feed permitted      = true
watchdog_ok         = false
```

## 5. Z-014 implementation already present in this worktree

The current tree already contains substantial mid-stage implementation:

- `lib/ams_core/watchdog/ams_watchdog_policy.c`
- `lib/ams_core/include/ams_core/ams_watchdog_policy.h`
- `lib/ams_core/watchdog/ORACLE_PROVENANCE.md`
- `include/ams_platform/watchdog.h`
- `drivers/ams/watchdog_zephyr.c`
- watchdog unit/differential/adapter tests under `tests/unit/watchdog/`
- watchdog validation config fragments in `app/`
- watchdog capability symbols in Kconfig/manifest code
- runtime watchdog integration in `app/src/ams_threads.c`
- proactive stack-query validity state in `ams_threads.h/.c`

### Z-013 stack-query defect being fixed as part of Z-014
Earlier Z-013 diagnostics collapsed `k_thread_stack_space_get()` failure into an apparent numerical zero. Z-014 must preserve the distinction between:

```text
query succeeded, unused == 0
```

and

```text
query failed / stack margin unknown
```

The current tree now contains explicit `stack_query_valid` and `stack_query_error_mask` state. The intended safety rule is: **query failure for an enabled/required thread is integrity unknown -> critical -> direct fail-low -> no watchdog feed.** Verify this end-to-end before closing Z-014.

FreeRTOS proactive stack thresholds must be preserved in byte-equivalent form on Cortex-M7:
- warning = `max(384 bytes, ceil(25% configured stack))`
- critical = `max(256 bytes, ceil(15% configured stack))`

Warning is diagnostic and does not stop feed. Critical does.

## 6. Exact Zephyr 4.4 STM32 IWDG findings already researched

The pinned Zephyr v4.4.0 STM32 IWDG driver was inspected before/while implementing Z-014.

Important facts from `drivers/watchdog/wdt_iwdg_stm32.c`:

1. `wdt_install_timeout()` **does not start hardware**. It calculates/stores prescaler and reload for later setup.
2. `wdt_setup()` calls `LL_IWDG_Enable()` **before** it waits for register update completion. Therefore a later setup error can occur after the irreversible hardware-start boundary.
3. STM32 IWDG cannot be stopped once started; Zephyr `disable` returns `-EPERM`.
4. `wdt_feed()` directly reloads the counter.
5. No callback should be used for the normal AMS reset watchdog path.
6. Do not use `WDT_OPT_PAUSE_HALTED_BY_DBG` for release/validation evidence; the v2.6.27 oracle did not freeze IWDG under debug.
7. Zephyr STM32 HWINFO maps the STM32 IWDG reset flag to `RESET_WATCHDOG`, so it can be used as reset-cause evidence on this family.

This is why the platform abstraction needs states richer than `bool started`, including an **ambiguous terminal start failure** after `wdt_setup()` is entered. Never blindly retry setup after a potentially irreversible start.

For a 5,000 ms timeout at Zephyr's STM32 F7 `LSI_VALUE=32000`, the driver's conversion chooses the first divider that fits the 12-bit reload: divider 64, reload 2499, matching the legacy nominal `/64, 2499` representation.

## 7. Two-mask migration evidence model

Do not pretend placeholder tasks are safety evidence.

Maintain:

```text
oracle_required_mask
migration_evidence_mask
effective_validation_mask = oracle_required_mask & migration_evidence_mask
```

At the Z-014 migration stage, the real evidence-ready safety actors are expected to be FAN and IMD. Current/ADBMS/CAN/TEMP are not yet all real migrated safety actors. Therefore:

```text
watchdog_full_oracle_coverage = false
BMS authority                 = false
balancing authority           = false
```

Even after IWDG hardware validation succeeds, vehicle authority remains forbidden until future stages make the evidence mask equal the oracle-required mask.

## 8. Z-014 sub-gates / exact continuation plan

### Z-014A — oracle + Zephyr driver freeze
**Status:** substantially done.
- v2.6.27 watchdog semantics frozen in `Z014_WATCHDOG_IWDG_PLAN.md`.
- Zephyr 4.4 STM32 IWDG behavior inspected.
- irreversible setup ambiguity identified.

**Closeout:** make sure implementation/docs record exact source version/hash and no unresolved API assumption remains.

### Z-014B — portable watchdog policy
**Status:** implementation/tests exist in current tree; must be re-audited and rerun.

Requirements:
- no Zephyr/RTOS/platform dependency;
- exact startup/heartbeat boundaries;
- process faults excluded from starvation policy;
- stable legacy block-reason numbering;
- saturation/wrap behavior;
- two-mask coverage semantics;
- start/retry/feed decisions represented as deterministic outputs.

Evidence target:
- directed boundary tests;
- exact oracle differential, minimum 100k stateful operations x 5 seeds x O0/O1/O2/O3/Os = 2.5M comparisons;
- null-platform build includes watchdog core.

### Z-014C — proactive stack integrity parity
**Status:** partially implemented in `ams_threads.c/.h`; requires careful audit.

Requirements:
- explicit query-valid state;
- warning/critical byte thresholds equivalent to FreeRTOS;
- warning does not starve;
- critical does;
- query failure is fail-closed integrity unknown;
- only enabled/created threads are scanned;
- scan WCET accounted for in 50 ms supervisor;
- actual MPU/HW stack protection remains enabled as second line of defense.

### Z-014D — platform-neutral watchdog API + Zephyr adapter
**Status:** files exist; requires audit/tests.

Layering:
`app -> ams_platform/watchdog.h -> drivers/ams/watchdog_zephyr.c -> Zephyr watchdog API -> STM32 IWDG`.

Adapter owns mechanism only: device readiness, install timeout, irreversible setup, feed, reset-cause/status. It must not know battery/process fault policy.

Required platform states should distinguish at least:
- UNPREPARED
- READY_NOT_STARTED
- PREPARE_FAILED_RETRYABLE
- STARTED
- START_AMBIGUOUS_TERMINAL
- FEED_FAILED_TERMINAL

Retry only when failure is provably before possible hardware enable.

### Z-014E — DTS/Kconfig/capabilities
**Status:** partially present.

Use standard Zephyr watchdog binding and a stable application alias such as `ams-watchdog = &iwdg`; do not invent a Drexel binding for a standard peripheral. Keep adapter-present, active, physical-validated, and full-oracle-coverage as separate facts.

Normal/base migration build should not accidentally arm an irreversible watchdog. Explicit validation configuration should control hardware activation. Authority remains off in every Z-014 configuration.

### Z-014F — safety-supervisor integration / sole-feed ownership
**Status:** integration code exists; must be deeply audited.

Intended 50 ms ordering:
1. safety cycle begins;
2. evaluate heartbeat stale state;
3. scan proactive stack integrity;
4. construct portable watchdog-policy snapshot;
5. force BMS low immediately for integrity/liveness failure;
6. evaluate watchdog policy;
7. perform allowed start transition if needed;
8. feed **exactly once near end of successful safety cycle** if policy permits and hardware is started;
9. update real feed/block telemetry;
10. publish cycle completion and re-anchor timing.

No other feed caller may exist.

### Z-014G — contracts / manifest / host closeout
**Status:** not yet accepted.

Add/verify watchdog-specific contract and include it in `check_all_contracts.py`. Contracts must check architecture, sole feeder, DTS alias/device, Kconfig capability consistency, authority lockout, stack query fail-closed semantics, source ownership, and null-platform inclusion.

Run focused host suites plus GCC `-fanalyzer`, Clang analyzer, ASan/UBSan. Do not rerun unrelated expensive SoP campaigns unless Z-014 actually changes those sources.

### Z-014H — target build + physical IWDG validation
**Status:** NOT RUN on this Z-014 tree.

First target build base configuration, then full unified contracts. Then build explicit watchdog-active validation configuration.

Physical validation must include:
- healthy feed endurance;
- deliberate stop-feed -> BMS low -> no feed recovery -> IWDG reset near nominal 5 s;
- reset cause confirms watchdog;
- block/kill safety supervisor while lower workers remain alive -> watchdog reset;
- starve each **real evidence-ready** source (currently fan and IMD) and verify timeout boundary plus at most one 50 ms supervisor period before starvation;
- persistent physical/process fault while task heartbeat remains healthy -> BMS low but watchdog continues feeding (no reset loop);
- stack warning -> feed continues;
- stack critical -> fail-low + feed stops before actual overflow;
- stack query failure/integrity unknown -> fail-low + feed stops;
- actual guard/MPU violation remains fatal/fail-low.

Do not set `CONFIG_AMS_WATCHDOG_TARGET_VALIDATED=y` merely because the image compiles.

## 9. Recommended immediate next actions in the new chat

1. Use this packaged tree as the exact starting point; do not regenerate Z-014 from prose.
2. Inspect all current Z-014 diffs/files before editing; this is mid-implementation.
3. Audit `ams_watchdog_policy.*` against the frozen v2.6.27 semantics.
4. Audit `watchdog_zephyr.c` specifically around the irreversible `wdt_setup()` boundary and ensure no retry after ambiguous setup failure.
5. Audit `ams_threads.c` stack-query validity and sole-feed ordering carefully.
6. Finish watchdog contract + unified gate + manifest capability truthfulness.
7. Run focused host/differential/adapter/analyzer tests.
8. Package a new full repo for the user to run `west build` locally.
9. Only after target compile/contracts are green, proceed to explicit IWDG-active hardware tests.

## 10. Commands expected for target closeout

Base target build (exact directory name can be changed):

```powershell
Set-Location 'C:\DER_AMS\git\revert\DER27-AMS-zephyr'

west build `
    -p always `
    -b der26_ams `
    app `
    -d build\z014_der26_ams

py scripts\check_all_contracts.py . build\z014_der26_ams
```

The explicit watchdog-active build should use the dedicated Z-014 config fragment only after its CMake/west invocation is verified against the current tree. Do not guess the invocation if it has not yet been wired into the build interface.

## 11. Non-negotiable safety/architecture invariants

- BMS_OK assertion authority remains OFF through Z-014.
- Balancing authority remains OFF.
- Physical validation claims remain false until physically demonstrated.
- Direct emergency fail-low remains board-specific and scheduler-independent.
- `lib/ams_core` stays RTOS/platform free.
- Hardware adapters contain mechanism, not AMS safety policy.
- No placeholder heartbeat counts as migrated safety evidence.
- Safety supervisor is sole watchdog feeder.
- Process faults do not automatically cause watchdog starvation/reset loops.
- Critical/unknown stack integrity stops feed and forces fail-low.
- Never retry an STM32 IWDG setup after entering an ambiguous irreversible start state.
- Do not use normal watchdog disable as a recovery mechanism; STM32 IWDG is irreversible after start.
- Full-oracle watchdog coverage remains false until future current/ADBMS/TEMP/CAN/estimator evidence is genuinely migrated.

## 12. Future migration direction after Z-014

Z-014 is infrastructure, not authority restoration. After it, continue migrating real safety evidence actors while preserving the abstraction boundary. Current actor integration, ADBMS protocol/transport separation, temperature path, CAN scheduler/transport/bus-off separation, estimator authority evidence, BMS authority and balancing authority each need explicit future gates. In particular, CAN must preserve the hardened v2.6.27 repeated-bus-off semantics: physical BOFF event identity, event sequencing so repeated events cannot collapse, third genuine event in sliding 10 s latching at the event boundary, protected recovery settlement, and authority restoration only after the protected frame set completes on wire.

Do not collapse those future stages into Z-014.
