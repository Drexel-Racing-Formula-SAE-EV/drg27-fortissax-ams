# Z-017 hardware validation checklist

Date: 2026-09-08

Status: **OPEN — no item below is claimed by source/host/SIL closeout.**

The Z017 target/hardware run is intentionally no-authority. BMS_OK must remain low/inhibited and balancing must remain inactive throughout. Establish the Z016 String-B physical-link baseline before treating any Z017 monitor result as meaningful.

## Required build profiles

Run fresh Zephyr 4.4.0 STM32F767 builds and `scripts/check_all_contracts.py` independently for:

1. base no-authority image;
2. IWDG validation image;
3. Z016 finite String-B probe image;
4. Z017 cell-validation image using `app/z017_cell_validation.conf`.

The Z017 build must prove `CONFIG_AMS_Z017_CELL_VALIDATION=y`, `CONFIG_AMS_Z016_LINK_PROBE=n`, acquisition liveness true, ADBMS safety evidence false, and absence of Z016 probe symbols.

## Physical String-B / isoSPI baseline

- [ ] String B / PE4 only; no String-A fallback or jumper ambiguity.
- [ ] Capture B-only wake timing and CS-low/high intervals.
- [ ] Capture SPI6 mode/frequency and representative command frame on target.
- [ ] Prove CS A remains inactive during all Z017 traffic.
- [ ] Verify normal cleanup leaves both CS lines inactive.
- [ ] Verify disconnected/corrupted chain returns bounded failure with no repeated uncontrolled traffic.

## Identity/configuration/startup

- [ ] RDSID identifies the expected ADBMS6830B.
- [ ] CFGA write/readback equals `81 00 00 FF 03 03` before POST.
- [ ] CFGB write/readback equals `71 52 46 00 00 00` before POST.
- [ ] Baseline diagnostic acquisition is clean enough to proceed.
- [ ] All eight FLAG_D POST stages produce the expected response with no unexpected bits.
- [ ] SPIFLT stimulus is observed as expected.
- [ ] Production CFGA restoration is captured after POST.
- [ ] Final config readback equals production vectors.
- [ ] Startup MUTE is observed.
- [ ] MUTE_ST readback is proven.
- [ ] DCC/timer state is zero.
- [ ] PWMA/PWMB are zero and zero readback is proven.
- [ ] No UNMUTE command appears anywhere in the trace.

## Cell acquisition

- [ ] Current-board ADCV is `03 E0`.
- [ ] 3 ms reference pre-wait is met.
- [ ] 17 ms conversion wait is met.
- [ ] SNAP precedes raw group reads; UNSNAP closes every successful/failed epoch.
- [ ] Raw C groups A..F map cells 1..16 correctly; cell 16 is read but not monitored.
- [ ] Fifteen monitored cells are plausible and compared with an external measurement.
- [ ] External comparison records absolute/error distribution, not only “looks plausible.”
- [ ] Status C/CCTS is nonzero on accepted epochs.
- [ ] Status D is captured as diagnostic-only evidence.
- [ ] AVG8 remains independent advisory data.
- [ ] IIR remains unusable until two complete filtered epochs at least 100 ms apart.
- [ ] Corrupt one mandatory raw packet and verify one complete epoch retry without a second ADCV.
- [ ] Corrupt optional AVG8/IIR packets and verify raw C authority is not replaced by an optional product.
- [ ] Force failed/uncertain SNAP and prove cleanup UNSNAP occurs before another SNAP.
- [ ] Force failed/uncertain UNSNAP and prove a new epoch does not begin until cleanup is positively established.

## Scheduling/ownership

- [ ] Measure the priority-3 ADBMS owner cadence at 100 ms.
- [ ] Verify no catch-up burst after an intentional overrun.
- [ ] Verify cooperative 3/17/20 ms waits do not starve higher-priority safety work.
- [ ] Verify wrong-thread/ISR transport access is rejected.
- [ ] Verify a stalled/failed timing source produces bounded failure.

## Authority containment

- [ ] BMS_OK remains low/inhibited for the entire Z017 validation.
- [ ] Balancing remains inactive except for startup-safe MUTE/zero commands.
- [ ] No estimator or pack-measurement authority consumes Z017 cell data.
- [ ] No temperature-safety evidence is fabricated.
- [ ] No ADBMS watchdog safety heartbeat is emitted.
- [ ] No APM/COMM traffic is present.

Only after this checklist and the four profile target-contract builds are complete should Z017 be called target/hardware validated. Z018+ remains out of scope.
