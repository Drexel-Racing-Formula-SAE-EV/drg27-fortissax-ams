# Z-017 String-B one-ADBMS6830 initialization/cell acquisition — software closeout

Date: 2026-09-08

Status: **source/host/SIL GREEN; target and physical validation remain separate open gates.**

## Boundary

Z017 is the no-authority one-SMB bench-validation profile selected from DER26 AMS v2.6.27 / FW 0.5.30. It uses String B / PE4 only through the temporary ADBMS6822 evaluation path, one physical ADBMS6830B, sixteen register cell channels with cells 1..15 monitored, raw C as the only voltage authority inside the driver, and AVG8/IIR as independent advisory products.

No String-A fallback, APM/COMM, temperature-safety evidence, estimator publication, combined pack measurement authority, BMS_OK assertion authority, balancing authority, or ADBMS watchdog safety heartbeat is introduced.

The controlling source hashes are frozen in `docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt`; the interpretation and intentional hardenings are documented in `Z017_ORACLE_FREEZE_2026-09-08.md`.

## Architecture

The existing priority-3 / 100-ms ADBMS thread remains the sole production owner. Z016 and Z017 are mutually exclusive build profiles. The owner binds once to the private SPI6 transport and Z017 exposes no public raw SPI/opcode/string-selection interface.

`lib/ams_core` contains the portable protocol, monitor state machine and cell-image logic. `drivers/ams/adbms_monitor_zephyr.c` is the narrow Zephyr adapter. All physical traffic is String B. The owner schedule remains absolute and skips missed historical releases rather than issuing catch-up bursts after long initialization/acquisition work.

## Startup implemented

The bounded startup state machine performs:

1. checked wake + SRST;
2. 300-us reset settle;
3. SID read and exact ADBMS6830B identity qualification;
4. production CFGA `81 00 00 FF 03 03` and CFGB `71 52 46 00 00 00` writes;
5. exact configuration readback;
6. diagnostic baseline;
7. eight FLAG_D POST stages;
8. dedicated SPIFLT stimulus;
9. mandatory production restoration;
10. final baseline + exact config readback;
11. startup-only emergency balancing inhibit: MUTE, MUTE_ST proof, DCC/timer zero, PWMA/PWMB zero, physical readback.

POST has at most two attempts. A failed mandatory restore is deliberately terminal for initialization rather than allowing a later retry to hide unproven configuration state. `UNMUTE` is not in the Z017 command inventory and no nonzero balance command/API exists.

## Acquisition implemented

The selected current-board sequence is:

`wake -> 3 ms reference prewait -> ADCV 03 E0 -> 17 ms wait -> [coherent epoch]`

Each epoch performs SNAP, 10-us settle, raw C A..F, Status C/CCTS, Status D, AVG8 A..F, IIR A..F, and UNSNAP cleanup. A first critical epoch failure permits exactly one complete epoch retry without issuing another ADCV.

Snapshot ownership is conservative: from the point a mutating SNAP may have reached the remote monitor, cleanup remains owed until UNSNAP is positively completed. Lost local session state, uncertain SNAP result, post-SNAP clock failure, failed cleanup wake and uncertain UNSNAP cannot permit another SNAP until cleanup is proven.

Raw C remains authoritative. Status-D/AVG8/IIR failure cannot substitute another product for raw C. IIR numeric values may be retained diagnostically but `iir_usable_mask` remains zero until two complete filtered epochs at least 100 ms apart establish readiness.

## Cell-image contract

- 16 register channels decoded, first 15 monitored;
- exact invalid sentinel handling, signed-code conversion checks;
- 500..5000 mV plausibility inclusive;
- retained maximum age 2500 ms;
- maximum two consecutive misses;
- 250 mV jump threshold;
- 120 unchanged-sample stuck threshold;
- wrap-safe timestamp age;
- saturating miss/unchanged counters;
- retained usable history is distinct from current-epoch freshness.

## Focused host/SIL evidence

Focused Z017 tests cover startup/configuration/POST, balance inhibit and readback, all counter transitions, snapshot uncertainty and cleanup, exhaustive startup transfer-fault injection, acquisition read-fault injection, mandatory-group session expiry, coherent retry/no-second-ADCV behavior, raw/AVG8/IIR independence, CCTS, invalid and out-of-range data, 2499/2500/2501-ms retention boundaries, miss 2/3, timer wrap, jump/stuck/saturation behavior, and 50,000 randomized cell-history steps.

The exact production monitor core is run under normal optimization, ASan, UBSan, GCC `-fanalyzer`, and Clang static analysis. The Zephyr adapter host test proves the String-B-only owner seam, cooperative timing path and copied diagnostic snapshot. Z016 differential PEC/counter/link tests and its finite read-only probe remain retained.

Z017 source/target contracts are profile-aware and the Z017 negative suite rejects **26** deliberately unsafe profile/protocol/ownership/coherence/authority mutations. The inherited Z016 negative suite rejects **12**.

## Intentional differences from the FreeRTOS oracle

These are safety hardenings, not silent parity claims:

- failed mandatory POST restoration terminates initialization;
- uncertain mutating SNAP state conservatively requires cleanup;
- IIR readiness is explicitly qualified by >=100 ms between complete epochs;
- the recursive FreeRTOS SPI mutex is not ported because the Zephyr ADBMS path has a single explicit owner;
- startup balancing support is inhibit-only and intentionally has no UNMUTE/nonzero-authority surface.

## Final canonical host/source/SIL closeout

The final Z017 runner completed successfully after all implementation, FreeRTOS-comparison fixes, contract hardening, and exhaustive focused fault campaigns:

```text
PASS: complete Z017 host/source/SIL closeout
13/13 Z017 composite stages passed
59.403 s
Inherited Z015 canonical report: success, 56/56 stages, 323.540 s
Inherited ThreadSanitizer evidence: present
Skipped evidence: none
```

Final Z017 evidence:

- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_REPORT_2026-09-08.json`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_REPORT_2026-09-08.json`
- `docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt`

SHA-256:

```text
51429d91f920586d07a3a62b11d8e5da1a4e8af6182d29a1b33627b34e37ea97  Z017_HOST_SIL_CANONICAL_2026-09-08.log
e5133dfe951bf4025dad5e6cb5da1e0f4d2bc9c96c60910a013f8af3cc160512  Z017_HOST_SIL_CANONICAL_REPORT_2026-09-08.json
c2106c2cdb98cd5001cc678b075fd6df0f7676f7790a422205b9d6dd697bd717  Z017_INHERITED_Z015_HOST_SIL_CANONICAL_2026-09-08.log
25770ecb554a2fa2966ff099c0c59d843d997242ad7b2b3a16a811b3b6cd7f6e  Z017_INHERITED_Z015_HOST_SIL_REPORT_2026-09-08.json
9788019e96a0ba49bcabbc4a58943d04bbca950f4cdd4e3d7f4d8ec10fbb4afb  Z017_ORACLE_SHA256SUMS_2026-09-08.txt
```

The Z017 runner explicitly records `target_build_performed=false`, `hardware_validation_performed=false`, `authority_promoted=false`, and `later_migration_stage_performed=false`.

## Remaining target/hardware gates

Host/source/SIL completion does **not** close Z017-G by itself. Fresh base, IWDG, Z016 probe and Z017 target builds plus `scripts/check_all_contracts.py` are required. Physical validation remains exactly the separate checklist in `Z017_HARDWARE_VALIDATION_CHECKLIST_2026-09-08.md`.

Until that evidence exists, do not claim physical SPI/isoSPI validation, cell-voltage accuracy, POST-on-hardware completion, scheduling measurements, BMS authority, balancing authority, or Z018+ completion.
