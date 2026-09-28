# Z023 — fault supervision and genuine heartbeat integration

Status: proposed implementation stage; planning only. Based on the restored Z022
checkpoint updated on 2026-09-15. This defines a new stage after the supplied
migration sequence, which previously ended at Z022.

## Goal

Connect the existing acquisition, measurement and current-fault workloads to a
coherent supervisor view and trustworthy progress reporting. A responsive task,
a valid measurement and a safe operating condition must remain separate facts.

Z023 targets the current single-SMB bench profile: String B, one 6830B, 15 cells,
24 temperatures, 20 ms current worker and 100 ms ADBMS releases. Temperature
scanning stays enabled; the owner has already validated the temperature hardware
and pull-ups. Remaining hardware checks concern the migrated Zephyr behavior.

BMS_OK assertion, balancing, AIR/charge control, APM/mixed-ring activation and
SoH/SoP authority remain disabled. The segment estimator stays advisory. Z021's
new-board integration is a separate dependency for eventual full-pack operation,
not a prerequisite for supervising the available SMB.

## Baseline and gaps

Z022 now has bounded ADC sampling, a shared current-window mutex, boundary time
captured after locking, immutable pinned publication, segment-local EKFs,
startup/precharge current-fault classification and copied diagnostics. It does
not yet connect those fault results to the supervisor or promote CURRENT,
ADBMS and TEMP watchdog evidence.

Before integration, audit these boundaries rather than treating the earlier
host PASS as proof of them:

- First-caller owner binding and attempted reentry from other threads/ISRs.
- Current update loss, publication drops and stale estimator output.
- Fault debounce under skipped releases: preserve the oracle's 20 ms accounting
  of completed current samples; supervise real elapsed age separately. Do not
  count unobserved time as valid samples or silently redesign debounce.
- Startup MUTE/zero proof, recovery generations and temperature scan coverage.
- Calibration provenance and the distinction between advisory current and
  qualified safety evidence.

## Required semantics

| Fact | Meaning | Does not prove |
|---|---|---|
| Worker progress | A new bounded owner cycle reached its required completion point | Successful sensing |
| Measurement validity | Data passed integrity, age and plausibility checks | Absence of overvoltage, overtemperature or another process fault |
| Process fault | The classified outcome requires inhibition or other defined handling | A dead task |
| Watchdog evidence | Qualified workload and supervisor integration cover that actor | Physical validation or BMS_OK permission |

A disconnected sensor can produce a fresh **invalid/faulted** result and genuine
worker progress. Copying the same old result cannot renew progress or freshness.
A terminal device fault may report progress only through an explicitly executed
owner fault-handling cycle whose new outcome reaches supervision; repeatedly
returning a cached error is insufficient. Its measurement remains unavailable.

## Implementation sequence

### Z23-A — freeze the supervision contract

Inventory the actual FreeRTOS safety/current/ADBMS call graph and record source
hashes, fault thresholds, debounce, latch/reset rules and startup behavior. Reuse
existing tested policies where possible. The 500–5000 mV acquisition plausibility
range is not a replacement for the electrical under/overvoltage policy.

Define typed faults for current invalidity/overcurrent, unavailable or stale
cells/temperatures, configuration/identity/PEC/counter faults, unresolved cleanup,
terminal recovery failure, and lost publication/worker progress. Distinguish
recoverable, latched process and execution-integrity faults explicitly.

Maintain the bench's STATE_START/precharge current policy. No charge or drive
mode is inferred. Freeze any unresolved electrical threshold before enabling its
supervisor rule; do not manufacture a value to make a test pass.

Deliverable: fault/outcome table with exact provenance and fail-low action for
each case, plus an explicit list of still-unqualified evidence.

### Z23-B — copied supervisor input and decision

Add a portable supervisor input/decision type and a Zephyr collector. Collect
copied, timestamped measurement and fault records with generation/sequence
identifiers. Record which checks require matching epochs; independent current
fault updates may be newer than a cell epoch and must not be falsely relabeled
as simultaneous measurements. Reject stale, future or incompatible provenance.

Read evaluation time under the relevant synchronization, preserving Z022's
current lock → boundary timestamp → rotation order. Establish and test a lock
order; no SPI, ADC conversion or reader-pinned payload copy inside a supervisor
spinlock. The supervisor never waits on the ADBMS bus.

Publish the latest classified decision and sticky history. A later good sample
must not silently erase a latch. Keep normal fail-low handling distinct from
fatal execution-integrity handling; do not route every sensor failure to reboot.

### Z23-C — owner progress records

Define bounded, copied completion records: actor, generation, cycle ID,
completion timestamp and outcome. Bind owners explicitly before use or prove
first-use serialization. Reject ISR, wrong-owner, duplicate and obsolete records.
Use a progress mechanism that remains unambiguous at counter saturation/wrap;
existing saturating diagnostic totals alone are not a freshness token.

Completion points:

| Actor | Required new work before progress is accepted |
|---|---|
| CURRENT | Bounded read attempt, conversion/invalidity classification, fault update and current-window disposition; record lock failure explicitly |
| ADBMS | Required acquisition or bounded recovery/terminal handling, cleanup disposition and publication or explicitly handled publication failure |
| TEMP | All eight distinct mux positions in one scan generation have received terminal processing outcomes for their three sensors |

For TEMP, preserve separate masks for converted, failed and suppressed/unattempted
sensors. A transport failure must not fabricate three successful conversions.
A full set of processed outcomes proves scan handling, not 24 valid temperatures.
Repeated positions cannot complete a scan; recovery invalidates partial coverage.
Normal scan duration remains eight 100 ms releases, with no catch-up burst.

### Z23-D — supervisor and watchdog handoff

The supervisor consumes each new qualified progress record once. Commit the
classified outcome before renewing its watchdog heartbeat. No heartbeat from a
scheduler iteration, copied diagnostics alone, estimator prediction, stale
measurement or repeated cached terminal result.

Preserve the frozen watchdog limits:

| Actor | Timeout |
|---|---:|
| CURRENT | 200 ms |
| ADBMS | 3000 ms |
| TEMP | 3000 ms |

Unseen actors become stale at startup age ≥3000 ms. Seen actors remain fresh at
exactly their timeout and become stale only above it. Preserve wrap-safe timing.
The estimator stays outside the required safety mask while SoP authority is off.

Keep the full oracle required mask intact. CAN remains unqualified, so adding
CURRENT/ADBMS/TEMP does not make full watchdog coverage true. Preserve the
existing IWDG validation policy; do not shrink requirements to obtain a green gate.

### Z23-E — profiles and evidence

Add `AMS_Z023_SUPERVISION_VALIDATION`, depending on Z022, and an explicit profile
file. All earlier profiles retain their previous behavior. Update capability,
source, target/ELF and build-manifest contracts together.

The validation profile may exercise real progress and shadow supervisor
outcomes while safety-evidence capabilities remain false. Promote CURRENT,
ADBMS or TEMP evidence individually only after its complete fault/supervision
contract and required physical evidence pass. Never promote all three merely
because the profile was selected. Physical-validation flags remain separate.

Preserve validated calibration record IDs, polarity, range and uncertainty.
Do not auto-zero, fabricate a calibration record, treat zero uncertainty as
proof, or promote unqualified current to trusted estimator/safety input. An
unqualified current path can still be exercised with evidence withheld.

### Z23-F — directed and negative testing

Use production portable logic and compile/execute the actual Zephyr adapter
branches under SIL. Test:

- Every typed fault and latch/reset transition; successful handling of invalid
  data versus a stalled worker or failed supervisor handoff.
- Current boundary race, mutex timeout, missing update taint, pinned publication
  drop, duplicate publication, stale/future timestamps and estimator withdrawal.
- Healthy TEMP scan, all eight-position coverage, repeated positions, suppressed
  conversions, interruption/recovery generations and no catch-up bursts.
- Completion before fault commit, replayed records, wrong owner, ISR, terminal
  recovery, clock wrap, exact startup/deadline boundaries and counter exhaustion.
- No heartbeat on unhandled failure; no renewal from cached diagnostics.
- No authority promotion, no String A/APM activation, no missing-segment
  fabrication, no forged calibration, and no reduction of the CAN requirement.

Mutation tests must demonstrate rejection of each forbidden promotion/order
change. Run inherited Z022 and earlier campaigns; distinguish freshly executed
regression from historical evidence. Run available sanitizers/analyzers and
report unavailable tooling explicitly.

### Z23-G — target and physical closeout

Compile/check base, watchdog validation, Z016, Z017, Z018, Z019, Z020, Z022 and
Z023 profiles. Include optional diagnostic profiles where their branches interact
with progress coverage. Confirm symbols, RAM/stack use and capability exclusion.

On hardware, measure completion-to-supervisor latency, lock waits, scan duration
and watchdog gaps. Inject disconnected/corrupt chain, ADC errors, frozen workers,
publication contention and recovery faults. Verify BMS_OK remains low and no
balancing is enabled. These are Zephyr migration checks on the already validated
temperature hardware.

## Exit criteria and deliverables

Report these independently: source/host complete, target complete, hardware
complete, and actor-by-actor safety-evidence qualification. No blanket “Z023
closed” while a required gate is missing. Document remaining Z021/full-pack and
CAN dependencies rather than relabeling them as completed Z022 work.

Deliver: portable supervision policy, copied adapter records/decision view,
explicit profile, contracts and behavioral/mutation tests, source/target/physical
validation records, updated build manifest, and a ZIP verified against a fresh
SHA-256 manifest.

Proposed files: `ams_supervision.h/.c`, a platform supervision adapter, changes to
`ams_threads.c`, the measurement pipeline and monitor outcome reporting,
`check_z023_contract.py`, `check_z023_mutations.py`, and
`run_z023_host_validation.py`. Final boundaries should follow the Z23-A review;
no generic raw-SPI service queue is part of this stage.
