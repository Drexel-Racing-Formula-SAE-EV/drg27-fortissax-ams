# Z017 String-B / one-ADBMS6830 initialization and cell-acquisition package closeout

Date: 2026-09-08

Status: **source/host/SIL GREEN; target and physical validation open.**

This package closes the software/host side of Z017 only. It does not begin Z018 and does not promote BMS_OK, balancing, temperature, estimator, APM, or ADBMS-watchdog safety authority.

## Frozen profile

- DER26 v2.6.27 / FW 0.5.30 selected one-SMB bench-validation oracle;
- String B / PE4 only;
- one physical ADBMS6830B;
- 16 register cell channels read, first 15 monitored;
- raw C is the only authoritative voltage product inside the monitor;
- AVG8 and IIR are independent advisory products;
- exact production CFGA `81 00 00 FF 03 03`, CFGB `71 52 46 00 00 00`;
- current-board ADCV `03 E0`;
- no String-A fallback or multi-SMB expansion.

## Safety/recovery hardenings completed

- uncertain SNAP ownership requires positive UNSNAP cleanup before another epoch;
- a successful SNAP followed by local time failure cannot lose cleanup obligation;
- IIR usability remains zero until two complete filtered epochs at least 100 ms apart;
- startup ports the FreeRTOS emergency balance-inhibit subset: MUTE + MUTE_ST proof + zero DCC/timer + zero PWMA/PWMB + readback;
- no UNMUTE or nonzero balancing command exists in the Z017 command surface;
- failed mandatory POST restoration is fail-closed for initialization;
- one whole-epoch retry is allowed and never issues a second ADCV;
- raw/AVG8/IIR product validity and retained cell history remain independent.

## Final software evidence

- Z017 source/profile/ownership contract: PASS;
- Z017 unsafe mutations rejected: **26**;
- Z016 inherited mutations rejected: **12**;
- Z017 monitor directed/randomized/fault campaign: PASS;
- 50,000 randomized cell-history operations: PASS;
- exhaustive startup-transfer and acquisition-read fault positions: PASS;
- ASan / UBSan / GCC `-fanalyzer` / Clang analyzer: PASS;
- Z016 200,000 differential PEC/counter/session checks: PASS;
- inherited Z015 canonical: **56/56 stages, 323.540 s, no skipped evidence, ThreadSanitizer performed**;
- final Z017 composite runner: **13/13 stages, 59.403 s**.

Canonical files:

- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_REPORT_2026-09-08.json`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_REPORT_2026-09-08.json`
- `docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt`

## Package hygiene

The shipped source/evidence tree contains **328 manifest-tracked files** plus the root `Z017_WORKTREE_SHA256SUMS.txt` manifest. Generated host/target build directories, `__pycache__`, `.pyc`, analyzer objects, and unit-test executables are excluded. The final ZIP SHA-256 is reported externally at handoff time to avoid a self-referential package hash.

## Remaining gates

A fresh real Zephyr 4.4 STM32F767 target closeout is still required for four profiles: base, IWDG validation, Z016 read-only probe, and Z017 cell validation. Each build must pass `scripts/check_all_contracts.py`.

Physical Z017 evidence remains a separate bench gate: String-B link/wake timing, ADBMS6830B SID/config readback, POST, safe balancing inhibit readback, 15-cell comparison against external measurement, CCTS/counter behavior, corrupt/disconnected-link injection, and measured owner-thread timing. See `Z017_HARDWARE_VALIDATION_CHECKLIST_2026-09-08.md`.

No target-green or hardware-green claim is made by this package.
