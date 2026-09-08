# Z-014 continuation closeout — deep safety/SIL hardened source candidate

**Date:** 2026-09-07 (user-local)

**Current status:** **Z-014 source/host/SIL hardened candidate.** The current tree is deliberately still a no-authority migration image. It does not advance to Z-015, does not enable BMS_OK assertion or balancing, and does not claim physical IWDG/fan/IMD validation.

The frozen behavioral oracle remains **DER26 FreeRTOS v2.6.27 / firmware 0.5.30**.

## Historical target evidence before the deep hardening pass

The pre-hardening Z-014 source successfully built in the user's real Windows Zephyr 4.4 workspace for `der26_ams/stm32f767xx` and passed the complete unified target/source contract suite in both configurations:

### Base image

- Zephyr 4.4.0;
- `zephyr.elf` linked successfully;
- FLASH: **165,264 B / 2 MB (7.88%)**;
- RAM: **47,424 B / 384 KB (12.06%)**;
- `PASS: complete Z-014 target/source contract suite`.

### IWDG-validation image

- `app/z014_watchdog_validation.conf` merged successfully;
- `zephyr.elf` linked successfully;
- FLASH: **165,296 B / 2 MB (7.88%)**;
- RAM: **47,424 B / 384 KB (12.06%)**;
- `PASS: complete Z-014 target/source contract suite`.

Those runs also exposed and drove fixes for three checker portability defects rather than firmware defects:

1. hidden default-`n` Kconfig symbols may be absent from `.config`;
2. Windows path separators must be normalized before sole-feeder comparison;
3. generated Zephyr syscall C must not be scanned as AMS production source;
4. Visual Studio multi-config CMake requires explicit `Release` for build/CTest and File-API fallback when `compile_commands.json` is unavailable.

**Important:** the deep hardening described below changes production watchdog/runtime source. Therefore the historical target greens above are evidence about the pre-hardening tree only. The current packaged snapshot requires a fresh target rebuild before target-green status can be restored.

## Deep Z-014-only hardening completed

The review intentionally stayed inside the current migration boundary and compared the Zephyr implementation against the FreeRTOS safety philosophy rather than mechanically porting RTOS calls.

### Dedicated portable heartbeat oracle

A new RTOS-independent heartbeat core now owns the watchdog liveness semantics instead of deriving safety evidence from generic runtime diagnostics:

- `lib/ams_core/include/ams_core/ams_watchdog_heartbeat.h`
- `lib/ams_core/watchdog/ams_watchdog_heartbeat.c`

It preserves the frozen eight heartbeat IDs, exact timeout values, 3000 ms startup grace, seen/unseen boundary semantics, wrap-safe age arithmetic, gap tracking and saturating counters.

### Coherent heartbeat concurrency

Worker heartbeat publication and supervisor evaluation now share one bounded `k_spinlock`. The supervisor samples `k_uptime_get_32()` inside that same serialized region, producing a coherent `(now_ms, stale_mask)` snapshot for one complete feed decision. This removes the possibility of evaluating a newly published timestamp against an older supervisor time.

### Stronger proactive stack integrity

Percentage warning/critical thresholds now use the actual Zephyr stack object size paired with `k_thread_stack_space_get()`, not only the requested source macro. Configured stack sizes remain compile-time lower bounds. Stack-query uncertainty remains fail-closed critical.

### Defensive fail-closed policy behavior

Malformed/out-of-domain watchdog state cannot accidentally become permission:

- NULL policy input/state => `RTOS_INTEGRITY`;
- out-of-schema heartbeat/evidence/stale bits => `RTOS_INTEGRITY`;
- NULL heartbeat update => all-stale;
- invalid heartbeat kick => rejected;
- counters saturate rather than wrapping.

These additions do not change valid FreeRTOS-domain decisions.

### Executable fatal-path composition

The actual production `app/src/ams_safety.c` fatal path is host-compiled with fake Zephyr hooks and tested to preserve direct PE0 fail-low before panic bookkeeping/fatal halt. A mutation gate also rejects reordering fatal halt before direct BMS_OK fail-low.

### Architecture/source hardening

The current source-only gates now mechanically preserve:

- `lib/ams_core` independence from Zephyr, FreeRTOS/CMSIS, STM32 HAL/register headers, board headers and `ams_platform`;
- hardware ownership behind `drivers/ams` + `include/ams_platform`;
- the sole direct-register exception as the board-owned PE0 emergency fail-low primitive;
- no positive BMS_OK authority API;
- no balancing authority/surface in Z-014;
- static/no-heap runtime;
- no system-workqueue use for current safety runtime;
- exactly one platform watchdog feeder owned by the safety supervisor;
- no direct application `wdt_feed()` and no `wdt_disable()` path;
- FAN and IMD as the only real current watchdog-evidence actors;
- deferred CURRENT/ADBMS/TEMP/CAN/LOGGER/ESTIMATOR capability bits incapable of becoming watchdog evidence;
- frozen SoP/SoH/fuse portable sources unchanged by hash.

## Host/SIL evidence for the hardened source

The unchanged FreeRTOS comprehensive host safety suite was executed and ended with:

`ALL COMPREHENSIVE HOST INJECTION TESTS PASSED`

The hardened Zephyr current-scope evidence includes:

- **179** directed watchdog/stack checks;
- **128** directed heartbeat-oracle checks;
- **2,500,000** exact heartbeat differential operations;
- **2,500,000** exact watchdog-policy differential decisions;
- five seeds across GCC `-O0/-O1/-O2/-O3/-Os` for both differential campaigns;
- **397,681** production watchdog-adapter SIL checks;
- concurrency SIL: **100,000 FAN kicks + 100,000 IMD kicks + 150,000 supervisor steps**, zero failures;
- focused ThreadSanitizer watchdog-concurrency pass;
- fatal fail-low composition: **20 checks**;
- BMS_OK adapter: **19 checks**;
- current sensor: **76 checks**;
- current fault: **22 checks**;
- current ADC adapter: **516,547 checks** plus low-timeout/ambiguous-completion edge suites;
- fan policy: **688 checks**;
- fan PWM adapter: **150,679 checks**;
- IMD core: **57 checks**;
- IMD capture adapter: **776,535 checks**;
- measurement store: **8** regression/concurrency tests;
- current window: **9** regression classes;
- estimator: **89** checks;
- integrated Z-014 safety SIL: five seeds × 250,000 steps = **1,250,000 event steps**, with **162,990 supervisor decisions** and **488,707 real actor completions**;
- ASan: PASS;
- UBSan: PASS;
- TSan focused concurrency: PASS;
- GCC `-fanalyzer`: PASS;
- Clang static analyzer: PASS;
- true null-platform `ams_core`: PASS over **16 translation units**;
- source hygiene: PASS;
- FreeRTOS runtime/watchdog source parity: PASS;
- frozen power-core source identity: PASS;
- Python checker syntax: PASS;
- **18 unsafe contract mutations rejected**.

The full canonical host/SIL wrapper was subsequently executed to completion in one uninterrupted invocation:

```text
python scripts/run_z014_host_validation.py . --require-clang --tsan
```

It completed with **exit code 0**, **47/47 stages passed**, **47.005 s total runtime**, and **no skipped evidence**. This final run included the 18-case mutation suite inside the same canonical invocation, so the earlier staged-execution limitation is fully closed.

The long SoP/SoH/fuse algorithm campaigns were intentionally not repeated because those portable sources are hash-frozen and unchanged during Z-014. If any of those sources drift, the source-identity gate fails and those campaigns become mandatory again.

## Current authority and validation truth

These remain intentionally false/disabled:

- `CONFIG_AMS_BMS_AUTHORITY=n`;
- `CONFIG_AMS_BALANCE_AUTHORITY=n`;
- full watchdog-oracle coverage is false because deferred actors are not real producers yet;
- `CONFIG_AMS_WATCHDOG_TARGET_VALIDATED=n`;
- physical watchdog validation not performed;
- physical fan validation not claimed;
- physical IMD validation not claimed;
- no Z-015 work included.

## Required next target gate for this exact hardened snapshot

When target work resumes, rebuild this exact source rather than reusing the historical pre-hardening ELF.

Base image:

```powershell
Set-Location 'C:\DER_AMS\git\revert\DER27-AMS-zephyr'
west build -p always -b der26_ams app -d build\z014_deep_hardened
py scripts\check_all_contracts.py . build\z014_deep_hardened
```

IWDG-validation image:

```powershell
west build -p always -b der26_ams app -d build\z014_deep_hardened_iwdg -- "-DEXTRA_CONF_FILE=z014_watchdog_validation.conf"
py scripts\check_all_contracts.py . build\z014_deep_hardened_iwdg
```

Physical IWDG validation remains a later explicit Z-014 gate and should only be run when requested. It is not part of this source/SIL package closeout.

## Detailed review

See:

- `docs/migration/Z014_DEEP_SAFETY_ARCHITECTURE_SIL_REVIEW_2026-09-07.md`
- `docs/migration/Z014_HOST_VALIDATION_EVIDENCE_2026-09-07.json`
- `docs/migration/Z014_WATCHDOG_IWDG_PLAN.md`

## Bottom line

The current architecture does not need a broad redesign at Z-014. The primary weakness was depth and explicitness around watchdog heartbeat ownership, concurrency, stack accounting, fail-closed malformed-state behavior and contract self-testing. Those areas are now materially stronger while preserving the staged no-authority migration model and the FreeRTOS safety philosophy.

This package is therefore a **Z-014 deep safety/SIL hardened source candidate**, not a target- or hardware-validated closeout.
