# Z022 restored checkpoint: estimator and fault integration

The uploaded Z022 ZIP was restored and extended. These changes supersede the
previous checkpoint's statement that estimator execution and current-fault
classification were entirely disconnected.

Completed in this update:
- Actual estimator-thread dispatch consuming pinned, immutable measurements.
- Five segment-local EKF instances. Only segments with all 15 usable raw cells
  and usable temperature observations execute. Absent SMBs stay invalid.
- Oracle temperature averaging over usable sensors within -40 to 120 C.
- Duplicate/out-of-order publication rejection, 100 ms consumer freshness,
  1–1000 ms epoch timing, timer/sequence wrap and cumulative-charge continuity.
- Unknown calibration remains explicitly untrusted for startup acquisition.
  Advisory SoC steps can run; R0 adaptation and SoH/SoP authority remain off.
- Verified MUTE and durable-zero state qualify the balance-recovered flag only
  for coherent no-balancing bench epochs. No discharge authority is introduced.
- Oracle current-fault classification in the frozen STATE_START/precharge mode
  at the existing 20 ms period, with original startup-ignore/debounce behavior.
  This bench profile has no drive/charge state machine; no mode is inferred.
- Copied diagnostics for current faults, accepted current-window updates,
  completed current work, consumed estimator epochs, segment SoC and validity.
  These completion counters are diagnostics, not watchdog safety evidence.

Validation: run `python scripts/run_z022_host_validation.py .`. This executes
production consumer/adapter tests, ASan/UBSan, GCC analysis, 19 Z022 mutations,
current-window and concurrent pinned-store regressions, and the inherited
campaign. The new consumer tests cover real EKF execution, absent segments,
replays, stale data, invalid-current gaps, balance/temperature gating and wrap.
The exact executed result is recorded in `docs/migration/evidence/Z022_HOST_REPORT.json`.

Full Z022 is not declared complete. Remaining gates are current/calibration and
fault-to-supervisor qualification, genuine safety-heartbeat promotion, mixed/full
hardware acquisition, target builds and physical timing/migration validation.
The restored one-SMB profile remains useful independently of the unfinished
Z021 mixed-ring target path. Temperature scanning remains enabled and its prior
hardware/pull-up validation remains accepted.

No Z023 work was invented: the available migration plan stops at Z022 and does
not define the requested Z023 deliverable. A Z023 scope is needed before coding it.
