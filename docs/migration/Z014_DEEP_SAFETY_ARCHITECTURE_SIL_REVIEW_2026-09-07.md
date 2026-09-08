# Z-014 Deep Safety / Architecture / SIL Review — 2026-09-07

## 1. Scope and status

This review is deliberately bounded to the **current Z-014 watchdog/IWDG migration stage**. It does **not** advance the migration to Z-015, does not port ADBMS/CAN/temperature/AIR/charger authority, does not enable BMS_OK assertion or balancing authority, and does not claim physical target validation.

The frozen behavioral oracle remains **DER26 FreeRTOS v2.6.27 / firmware 0.5.30**. The current Zephyr tree is still a no-authority migration image. At Z-014, the only real migrated watchdog-evidence actors are **FAN and IMD**. CURRENT, ADBMS, TEMP, CAN, LOGGER and ESTIMATOR remain absent/deferred as appropriate to the current migration boundary and therefore cannot fabricate watchdog evidence.

The pre-review Zephyr tree had already passed a real Zephyr 4.4 target build and the complete target/source contract suite in both base and IWDG-validation configurations. This deep review changes production watchdog/runtime source, so those prior target results are useful historical evidence but **do not validate this hardened source snapshot**. The hardened tree must be rebuilt on the real Zephyr workspace before target status is called green again. Physical IWDG testing remains intentionally unperformed.

## 2. Oracle evidence used

The FreeRTOS oracle was treated as a behavioral specification rather than as code to mechanically rewrite. Its comprehensive host suite was executed unchanged:

- `make safety-test`
- **108 PASS lines**
- final result: `ALL COMPREHENSIVE HOST INJECTION TESTS PASSED`
- runtime: approximately **3.31 s** on the review host

The oracle suite covers substantially more than Z-014 because the FreeRTOS firmware is the completed operating system. That includes ADBMS, CAN bus-off/authority, temperature, charger, AIR, telemetry, balancing and other functionality that has intentionally not yet migrated. Those tests are not valid requirements to duplicate with placeholder Zephyr actors. The correct Z-014 comparison is to reproduce the oracle's **current migrated mechanisms and safety semantics**, while keeping deferred subsystems incapable of becoming authority or watchdog evidence.

## 3. Architecture conclusion

The basic Zephyr architecture is sound and should be preserved.

### 3.1 Portable core boundary

`lib/ams_core` remains ordinary host-native C and has no dependency on:

- Zephyr;
- FreeRTOS/CMSIS-RTOS;
- STM32 HAL/register headers;
- `ams_platform`;
- board-specific headers.

A true null-platform build compiles the core with Zephyr discovery disabled and currently audits **16 portable translation units**. This is the correct dependency direction: platform adapters consume portable types/policy, never the reverse.

### 3.2 Platform adapter boundary

Normal hardware ownership is isolated under `drivers/ams` and exposed through narrow interfaces in `include/ams_platform`:

- BMS_OK normal GPIO ownership;
- current ADC acquisition;
- fan PWM;
- IMD capture;
- watchdog/IWDG.

Application orchestration does not import Zephyr hardware-driver headers or Devicetree accessors. This is stronger and safer than allowing each thread to own peripheral details directly.

### 3.3 Emergency fail-low exception

The one deliberate abstraction escape hatch is the board-owned PE0 BMS_OK fail-low primitive:

`boards/drexel/der26_ams/ams_fail_low_stm32.c`

That direct-register path is appropriate because it must remain usable during pre-kernel initialization and fatal handling without depending on scheduler state, heap, logging, workqueues or the ordinary GPIO driver. It validates the board/SoC/PE0/active-high contract at compile time, preloads PE0 low before selecting output mode, forces it low again afterward, and ends with DSB/ISB barriers.

No positive/high BMS_OK API exists in the Z-014 platform surface.

### 3.4 Application composition layer

`app/src/ams_threads.c` is large, but it is currently functioning as the explicit composition root: thread topology, relative priority, scheduling, cross-cutting liveness and diagnostics are assembled there while hardware and portable policy stay outside it. Splitting it purely for file-size aesthetics during a safety migration would increase integration risk without creating a stronger safety boundary. A later split is reasonable only when concrete subsystem ownership justifies it.

### 3.5 Static/no-workqueue runtime

The current safety runtime is intentionally static:

- no application heap;
- no `k_malloc`/`malloc` family use;
- no system workqueue submission/scheduling;
- statically allocated thread stacks/objects;
- MPU + hardware stack protection required;
- initialized stacks + stack metadata required.

The source-only architecture gate now rejects regressions in these properties.

## 4. Safety findings and hardening completed

### 4.1 Watchdog evidence was too coupled to generic runtime diagnostics

The pre-review implementation derived watchdog stale evidence from generic per-thread runtime statistics. That was workable for the early skeleton but weaker than the FreeRTOS oracle, which owns a dedicated heartbeat monitor with explicit critical-section semantics.

**Hardening:** an RTOS-independent portable heartbeat core was added:

- `ams_watchdog_heartbeat.h`
- `ams_watchdog_heartbeat.c`

It preserves the exact eight heartbeat IDs, exact timeout values, startup-grace behavior, seen/unseen semantics, gap tracking, max-gap tracking, masks and saturating counters. Generic runtime statistics remain diagnostics only and no longer authorize IWDG feeding.

### 4.2 Heartbeat publication/supervisor snapshot race

A worker can compute a completion time and then contend with the safety supervisor. If heartbeat state and supervisor `now` are not serialized coherently, the supervisor can evaluate a newly published timestamp using an older `now`, and unsigned wrap arithmetic can fabricate an enormous age.

**Hardening:** heartbeat kick and supervisor update now share one bounded `k_spinlock`. The supervisor samples `k_uptime_get_32()` **inside the heartbeat lock** and returns one coherent `(now_ms, stale_mask)` snapshot. That same snapshot is reused for the complete watchdog decision/feed attempt in that supervisor cycle.

This is safe on the single-core STM32F767 and remains well-defined under host/SMP concurrency testing.

### 4.3 Stack percentage threshold denominator could be weakened by allocation expansion

The pre-review proactive 25% warning / 15% critical policy used the source-requested stack size while Zephyr's stack-space query reports against the actual stack object supplied to the kernel. MPU/alignment expansion could therefore make the practical percentage thresholds slightly weaker than intended.

**Hardening:** proactive percentage thresholds now use `desc->stack_size`, the actual `K_THREAD_STACK_SIZEOF(...)` value paired with the queried stack. Source configured sizes remain compile-time lower bounds against the FreeRTOS byte-equivalent oracle.

Query failure remains distinguishable from a true zero margin and is fail-closed critical.

### 4.4 Invalid policy state/masks were insufficiently defensive

Normal oracle inputs are valid, but safety code should not turn malformed state into permission.

**Hardening:** outside the valid oracle domain:

- NULL state/input cannot inherit feed permission; it returns `RTOS_INTEGRITY`;
- heartbeat/oracle/evidence/stale masks containing bits outside the frozen eight-bit schema return `RTOS_INTEGRITY`;
- NULL heartbeat update fails closed as all-stale;
- counters continue to saturate instead of wrapping to a value that could look "never seen".

These changes do not alter valid FreeRTOS-domain decisions.

### 4.5 Fatal fail-low path needed executable composition evidence

Static ordering already required BMS_OK fail-low before Zephyr fatal halt, but the final safety path deserved direct host execution.

**Hardening:** `tests/unit/safety_fatal` host-compiles production `app/src/ams_safety.c` with minimal Zephyr stubs and verifies:

- direct fail-low occurs before fatal halt;
- panic latch is visible before halt;
- a second fatal call repeats fail-low;
- `ams_safety_init()` propagates platform initialization status.

Current result: **20 checks PASS**, including strict warnings, ASan/UBSan, GCC analyzer and Clang analyzer passes.

### 4.6 Runtime-start failure after possible IWDG arm is now a source contract

In validation mode, `wdt_setup()` is intentionally crossed before application thread construction to preserve the oracle's heartbeat-init/watchdog-arm/task-create order. Therefore a thread-object creation failure must never return into a partially started application.

**Hardening:** the source-only hygiene gate now requires `ams_threads_start()` failure in `main()` to enter `k_panic()` before any post-start continuation. The fatal override independently forces BMS_OK low and the already-started IWDG is then allowed to expire if the system cannot recover.

### 4.7 Contract checker self-testing was too shallow

Earlier Windows target runs exposed two checker bugs: hidden Kconfig `n` symbols can be absent from `.config`, and generated Zephyr syscall C can legitimately contain `wdt_feed()` without being an AMS feeder.

**Hardening:** the checker itself now has mutation/negative-control coverage. It distinguishes production source from generated build source and interprets hidden-default-n Kconfig correctly.

## 5. Exact watchdog parity retained

The portable heartbeat source retains the FreeRTOS boundaries exactly:

| Heartbeat | v2.6.27 timeout | Z-014 portable timeout |
|---|---:|---:|
| ADBMS | 3000 ms | 3000 ms |
| CURRENT | 200 ms | 200 ms |
| TEMP | 3000 ms | 3000 ms |
| CAN | 2000 ms | 2000 ms |
| LOGGER | 2000 ms | 2000 ms |
| IMD | 500 ms | 500 ms |
| FAN | 1000 ms | 1000 ms |
| ESTIMATOR | 500 ms | 500 ms |

Additional exact semantics:

- startup grace: **3000 ms**;
- unseen heartbeat: fresh while startup age `< 3000 ms`, stale at exactly `3000 ms`;
- seen heartbeat: fresh at `age == timeout`, stale only when `age > timeout`;
- age arithmetic: unsigned 32-bit subtraction, preserving wrap behavior;
- heartbeat count: saturating, not wrapping;
- watchdog timeout: **5000 ms**;
- valid watchdog decision ordering: runtime disabled → panic → explicit stop-feed → startup grace → effective heartbeat stale → RTOS/stack integrity → healthy;
- ordinary process faults do not inherently starve the watchdog.

The legacy 3000 ms startup grace also masks heartbeat/RTOS-stack decisions during that interval except higher-priority panic/explicit stop-feed conditions. That is inherited oracle behavior and was intentionally **not** redesigned during migration.

## 6. One-way IWDG mechanism boundary

The watchdog platform adapter preserves the critical STM32/Zephyr distinction:

1. `prepare()` is retryable and pre-irreversible. It checks device readiness, captures reset cause and installs the timeout.
2. `start()` marks state ambiguous/terminal **before** calling `wdt_setup()` because STM32 IWDG may already have been enabled if a later Zephyr ready wait returns an error.
3. a start error after crossing that boundary is never retried;
4. only the safety supervisor may call the AMS feed API;
5. only the watchdog adapter may call Zephyr `wdt_feed()`;
6. any feed error is terminal integrity failure;
7. no `wdt_disable()` path exists.

The watchdog is therefore treated as a one-way safety mechanism rather than a recoverable convenience peripheral.

## 7. Two-mask evidence model remains intentionally partial

Z-014 deliberately distinguishes:

- **oracle-required mask** — what the completed FreeRTOS watchdog ultimately expects;
- **migration-evidence mask** — which of those actors have real Zephyr workloads capable of producing meaningful liveness now.

At this stage only FAN and IMD are real watchdog-evidence producers. Placeholder loops cannot satisfy the watchdog contract. `coverage_complete` therefore remains false by construction.

`health_good` currently means the **effective presently migrated evidence is healthy and the platform is started**. It does not mean full FreeRTOS watchdog coverage. This is diagnostic only and cannot grant BMS_OK/balance authority. The terminology is worth keeping explicit in diagnostics, but changing the policy field during this stage would create unnecessary oracle/test churn.

## 8. Current Z-014 host/SIL depth

The final hardened source has the following current-scope evidence:

### Watchdog / liveness

- **179** directed watchdog/stack checks;
- **128** directed heartbeat oracle checks;
- **2,500,000** heartbeat differential operations against an independent v2.6.27 reference model;
- **2,500,000** policy differential decisions against the watchdog reference oracle;
- five seeds × `-O0/-O1/-O2/-O3/-Os` for both exact differential campaigns;
- **397,681** production Zephyr watchdog-adapter SIL checks;
- concurrency SIL: **100,000 FAN kicks + 100,000 IMD kicks + 150,000 supervisor steps**, zero failures;
- focused ThreadSanitizer concurrency run: PASS.

### Fatal/BMS safety

- fatal/fail-low production composition: **20 checks**;
- normal BMS_OK adapter: **19 checks**.

### Already-present current path

- current sensor: **76 checks**;
- current fault: **22 checks**;
- current ADC adapter: **516,547 checks**;
- low-timeout edge suite: **14 checks**;
- ambiguous-completion suite: **6 checks**.

The current ADC adapter remains linked/initialized but acquisition scheduling is deferred; it therefore does not become watchdog evidence.

### FAN / IMD

- fan policy core: **688 checks**;
- fan PWM adapter: **150,679 checks**;
- IMD core: **57 checks**;
- IMD capture adapter: **776,535 checks**.

### Portable measurement / estimator code already present

- measurement store: **8 regression/concurrency tests**, including concurrent-copy stress;
- current window: **9 regression classes** covering boundary, uncertainty/extrema, wrap, stale-tail, provenance and invalid/range behavior;
- estimator core: **89 v2.6.27 checks**.

### Integrated Z-014 safety SIL

The new `tests/system/z014_safety_sil` composes the real portable heartbeat, watchdog policy and stack-integrity core in an application-level model.

Deterministic classes cover:

1. startup-grace and exact stale boundaries/recovery;
2. deferred placeholder heartbeats cannot authorize;
3. process faults retain liveness while software starvation blocks;
4. stack warning/critical/query-failure behavior;
5. platform start/feed terminal-state behavior;
6. 32-bit tick wrap;
7. randomized actor/supervisor scheduling abuse.

Randomized campaign:

- **5 seeds**;
- **250,000 event steps per seed**;
- **1,250,000 total event steps**;
- observed **162,990 supervisor decisions** and **488,707 real actor completions**;
- BMS_OK and balancing authority structurally absent.

## 9. Sanitizer / analyzer depth

Current-scope suites have been exercised with:

- normal optimized host builds;
- strict warning profiles for watchdog and fatal composition (`-Wconversion`, `-Wsign-conversion`, `-Wshadow`, plus watchdog double-promotion checks);
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- ThreadSanitizer on watchdog concurrency;
- GCC `-fanalyzer`;
- Clang static analyzer.

The true null-platform build additionally proves that portable core compile paths do not acquire Zephyr/FreeRTOS/HAL dependencies accidentally.

## 10. Safety-contract mutation testing

The final mutation harness rejects **18 representative unsafe edits**:

1. direct application `wdt_feed()` bypass;
2. second AMS platform feeder;
3. placeholder actor promoted to safety evidence;
4. stack percentage weakened back to requested-size accounting;
5. supervisor heartbeat time sampled outside serialization;
6. BMS authority enabled;
7. unmigrated TEMP evidence enabled;
8. IWDG disable path introduced;
9. invalid-heartbeat-mask guard removed;
10. invalid-stale-mask guard removed;
11. heartbeat grace duplicated instead of sharing the policy truth;
12. platform/policy watchdog-timeout compile-time bind removed;
13. dynamic allocation introduced into production runtime;
14. raw MCU register ownership leaked into app source;
15. positive BMS_OK authority API introduced;
16. frozen SoP/SoH/fuse source drifted during Z-014;
17. fatal halt moved before direct BMS_OK fail-low;
18. runtime-start failure returns instead of entering panic/fail-low.

It also positively regression-tests that generated Zephyr syscall C is not mistaken for an AMS feeder and that a hidden default-`n` capability may legitimately be absent from `.config`.

## 11. FreeRTOS host-runner comparison

The FreeRTOS `safety-test` suite is much broader because it tests the finished operating firmware. For current Z-014 scope, the Zephyr tree now has direct equivalents or stronger evidence for the relevant mechanisms:

| FreeRTOS safety-test area | Current Z-014 evidence | Status |
|---|---|---|
| software heartbeat monitor faults/recovery | exact heartbeat portable oracle + 2.5M differential + integrated SIL | covered |
| concurrent heartbeat starvation/recovery | spinlock publication + concurrency SIL + TSan | covered |
| concurrent seeded scheduler abuse | integrated 1.25M-event scheduler SIL | covered for current actors |
| watchdog feed gate | exact policy differential + sole-feeder contracts | covered |
| watchdog boot arm/startup grace | policy/adapter directed tests + exact boundaries + startup-order contracts | covered in host/source; physical reset still deferred |
| watchdog start-failure fail-closed gate | adapter state-machine SIL + integrated system SIL | covered in host/source |
| RTOS stack/heap diagnostics | portable stack core + query-fail closed behavior + compile/source invariants | covered in host/source |
| safety panic/reset path | production `ams_safety.c` fatal composition test + mutation gate | covered except physical reset observation |
| fan/current/null guards | fan/current host suites + defensive watchdog/null checks | covered for migrated code |
| IMD capture validation | IMD core/adapter host suites + real actor heartbeat placement contract | covered in host/source; physical IMD validation remains false |
| measurement epoch/current-window | measurement-store + current-window regressions | covered for portable current tree |
| estimator core | 89-check portable estimator suite | covered for present portable core |

The following FreeRTOS categories are **deliberately deferred rather than missing Z-014 tests**: ADBMS transport/diagnostics/ring ownership, temperature acquisition/policy, CAN transport/bus-off/on-wire authority, charger commands, AIR monitor, balancing authority, full state/vehicle authority, telemetry and later estimator authority integration. Testing those against placeholder Zephyr loops would provide false confidence and violate staged migration.

## 12. Frozen SoP/SoH/fuse campaigns

The long SoP/SoH/fuse oracle campaigns were intentionally not re-run as part of a watchdog-only source hardening pass. Instead, `check_power_core_contract.py` has a source-only mode that verifies the canonical portable SoP/SoH/fuse files remain byte-for-byte at their frozen v2.6.27 hashes.

This is a deliberate scope control, not a silent test omission. If those sources change, the gate fails and the expensive algorithm campaigns become mandatory again.

## 13. Host execution closeout

The complete canonical current-scope gate was executed successfully in one uninterrupted invocation:

```text
python scripts/run_z014_host_validation.py . --require-clang --tsan
```

Final result:

- **exit code 0**;
- **47/47 recorded stages passed**;
- **47.005 s** total runtime;
- **no skipped evidence**;
- source parity/hygiene: PASS;
- frozen power-source identity: PASS;
- true null-platform build: PASS;
- all current unit and integrated system SIL: PASS;
- ASan/UBSan: PASS;
- focused watchdog TSan: PASS;
- GCC `-fanalyzer`: PASS;
- Clang static analyzer: PASS;
- all **18 safety-contract mutations rejected** inside the same canonical run.

The canonical evidence is retained under `docs/migration/evidence/`. No target build, hardware test, authority enablement or Z-015 work was performed by this host closeout.

## 14. Remaining Z-014 gates after this source review

No later migration work is authorized by this review.

Before this hardened snapshot can regain target-green status:

1. clean Zephyr 4.4 base target rebuild on `der26_ams`;
2. complete `check_all_contracts.py` against that new build;
3. clean IWDG-validation configuration build;
4. complete target/source contracts against that new build.

Only after the user chooses to begin physical work should the separate hardware IWDG validation matrix be executed. This review performs and claims **no physical watchdog, fan or IMD validation**.

## 15. Final assessment

At the current migration boundary, the architecture does not need a broad redesign. The major deficit was **watchdog evidence/integration test depth**, not the portable/platform layering itself.

The hardened Z-014 tree now more closely matches the engineering discipline of the frozen FreeRTOS system by making liveness a dedicated portable mechanism, making heartbeat publication atomic with respect to supervisor observation, using the actual queried stack allocation for proactive margins, failing closed outside valid policy inputs, executing the real fatal composition on host, and testing the safety contracts themselves with mutations.

The remaining uncertainty is now where it should be: **target compilation of this changed snapshot and eventual physical behavior**, not untested host semantics or accidental authority leakage.
