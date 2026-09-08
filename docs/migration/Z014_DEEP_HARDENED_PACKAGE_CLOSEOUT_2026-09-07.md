# Z-014 deep-hardened package closeout

**Date:** 2026-09-07 (user local)

This repository snapshot is the completed **Z-014 deep safety / architecture / host-SIL hardening package**. It remains deliberately inside Z-014. It does not begin ADBMS transport migration, CAN migration, BMS_OK assertion authority, balancing authority, or any Z-015+ work.

## Oracle

Frozen behavior reference: **DER26 FreeRTOS v2.6.27 / firmware 0.5.30**.

## What was hardened

- dedicated RTOS-independent watchdog heartbeat core with exact FreeRTOS timeout/grace semantics;
- coherent heartbeat publication + supervisor snapshot under one bounded spinlock;
- proactive stack thresholds based on the actual queried Zephyr stack object size;
- fail-closed malformed watchdog masks/inputs and stack-query uncertainty;
- production fatal path host composition proving direct PE0 fail-low before fatal halt;
- one-way IWDG platform state semantics and sole safety-supervisor feed ownership;
- explicit partial two-mask watchdog evidence model;
- source-only architecture/hardware ownership/no-authority contracts;
- negative/mutation tests against the safety checkers themselves;
- canonical host runner covering current-scope unit, differential, concurrency, integrated SIL, sanitizers and analyzers.

## Final host/SIL closeout

Canonical command:

```text
python scripts/run_z014_host_validation.py . --require-clang --tsan
```

Final result: **PASS**, exit code **0**, **47/47 stages**, **47.005 s**, **no skipped evidence**.

Key depth:

- 179 directed watchdog/stack checks;
- 128 heartbeat directed checks;
- 2.5 million exact heartbeat differential operations;
- 2.5 million exact watchdog-policy differential decisions;
- 397,681 watchdog-adapter checks;
- 100,000 FAN + 100,000 IMD heartbeat publications with 150,000 concurrent supervisor steps;
- focused TSan: PASS;
- 1.25 million integrated randomized safety-SIL event steps;
- ASan / UBSan / GCC `-fanalyzer` / Clang analyzer: PASS;
- true null-platform build over 16 portable-core translation units: PASS;
- 18 unsafe contract mutations rejected;
- unchanged FreeRTOS comprehensive safety suite: PASS.

## Scope truth

Still intentionally false/deferred:

- BMS_OK positive assertion authority;
- balancing authority;
- full watchdog oracle coverage for actors not yet migrated;
- hardened-snapshot target rebuild;
- physical IWDG validation;
- physical fan/IMD validation;
- Z-015 and later migration work.

The pre-hardening tree did build and pass target contracts in both base and IWDG-validation configurations, but those ELFs are **not** evidence for the hardened snapshot because production watchdog/runtime source changed during this review.

## Next gate when target work resumes

```powershell
Set-Location 'C:\DER_AMS\git\revert\DER27-AMS-zephyr'
west build -p always -b der26_ams app -d build\z014_deep_hardened
py scripts\check_all_contracts.py . build\z014_deep_hardened

west build -p always -b der26_ams app -d build\z014_deep_hardened_iwdg -- "-DEXTRA_CONF_FILE=z014_watchdog_validation.conf"
py scripts\check_all_contracts.py . build\z014_deep_hardened_iwdg
```

Do not reuse the historical pre-hardening build directory as evidence for this snapshot.
