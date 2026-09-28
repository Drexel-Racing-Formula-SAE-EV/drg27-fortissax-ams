# Z021 portable foundation — partial implementation

Z021 is NOT complete. This checkpoint retains the Z020 live runtime and its
String B / one-SMB profile with temperature scanning enabled and balancing off.
No Z021 target profile, APM traffic, String A selection, HV-divider enablement,
measurement publication or safety authority is introduced. Build manifests
continue to identify the selected actual runtime stage, not this checkpoint name.

## Implemented

- Canonical physical ordering for five SMBs then one APM from A; read and reverse
  write block indices for either endpoint across the complete six-device ring.
  These are full-ring indices, not an assumption that short subset frames reach
  every device. Mixed-command padding and actual frame construction remain open.
- A single-use full-ring wake token. All six devices and all five known SMB
  counters must be established. Caller supplies a physically justified wake
  budget; there is deliberately no fabricated default. Expiry uses wrap-safe
  unsigned elapsed time and rejects the exact deadline.
- Generation tickets consumed before APM work. Only the outstanding generation
  can complete. Successful UNSNAP/SNAP/reads/UNSNAP advances all five SMB counter
  expectations three times, including 0→1 and 63→1. Failed/uncertain outcomes
  invalidate every SMB prediction. UNSNAP debt survives interruption and can
  only clear on a matching-generation positive cleanup result. Generation
  exhaustion prevents reuse. `ring_reset` is first construction only; it is not
  an in-service recovery API. The future owner must enforce sole-thread access.
- Advisory primary ADBMS2950 RDSTAT/RDIVB1/RDFLAG decoding: PEC10 and expected
  command-counter equality, I1 calibration flag, exact 24-bit current/16-bit
  VB1 little-endian layout, reset/clear sentinels, conversion-count progress,
  signed scaling and explicit DER/EVAL calibration. Pack voltage validity needs
  caller-proven divider enablement. Nonfinite calibration/results are rejected.
  A failed decode clears output validity and cannot update conversion history.
  The future owner must prove identity/configuration, SNAP and UNSNAP, reset
  conversion history on a new ADI1 epoch/calibration, and enforce sample ageing.

The APM primary rules come from frozen v2.6.27 `adbms2950.c`, `.h`, and `_defs.h`;
shared-counter/token rules come from `accumulator_read_apm` in `accumulator.c`.
No unseen datasheet assumptions are used to manufacture a target driver.

## Validation

Run `python scripts/run_z021_host_validation.py .`.
The runner executes containment, production tests, ASan/UBSan, GCC analysis,
19 behavioral mutations (each must compile and then fail its test), and the
inherited Z020 composite on this source tree. Tests cover all six mappings,
all 64 counter values, all 64 wake masks, wrap/expiry, stale tickets, cleanup
uncertainty, all 192 single-bit corruptions of the three response packets,
valid-PEC counter mismatches, sentinel values, calibration, signed values,
conversion stall/wrap and divider-off validity. Logs/reports live in
`docs/migration/evidence`. Full completion and target integration are explicitly
false regardless of host test success. Clang and target build tools are absent.

## Required to finish Z021

1. Identify the new-board revision and provide its reviewed Zephyr board
   definition/pin mapping and intended six-device physical wiring. The current
   plan explicitly reserves mixed topology for a new-board profile; the existing
   board is frozen to the temporary String-B route. Schematics alone do not
   identify which new revision/profile is intended.
2. Implement an explicitly selected target profile and one owner for both device
   families, A-side SMB/B-side APM routing, full-ring wake and frame construction,
   SID/config/readback, reference-up and ADI1 initialization, bounded sessions,
   snapshot cleanup and restoration/recovery coordination. Requalify SMB state
   after APM mutations instead of guessing their counter effects.
3. Port advisory redundant I2/VB2 acquisition with its inverted scales, calibration,
   restoration and no primary substitution. Keep HV dividers off until the
   intended board/profile explicitly defines their permitted use.
4. Add adapter SIL and target contracts, rerun inherited/fault campaigns and
   compile target profiles. Physical mixed-ring/timing and measurement comparison
   are separate gates; no APM safety authority follows from successful reads.

No active-balancing or temperature-hardware restriction has been added. The user
has already validated temperature hardware/pull-ups; only migrated behavior needs
its corresponding Zephyr evidence.
