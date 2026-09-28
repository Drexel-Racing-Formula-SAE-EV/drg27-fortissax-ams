# Z024 portable CAN checkpoint — runtime integration incomplete

## 2026-09-22 continuation

Restored the saved checkpoint after the scratch worktree expired. Fixed a
completion deadline bypass: a callback whose event timestamp was beyond the
100-ms request deadline could previously clear outstanding ownership before
the timeout sweep, incorrectly incrementing the successful-completion count.
The event timestamp now enforces the deadline. A completion at exactly 100 ms
is accepted even when observed later; one at 101 ms inhibits transport without
credit. Directed tests also verify rejection of its old cookie after recovery.
The behavioral campaign now has 21 compiled negative controls.

The source-download blocker is now resolved. See
[the current review](Z024_REVIEW_2026-09-22.md) for five local fixes, inspected
upstream driver hazards and the remaining runtime integration work.

This repository adds the portable portion of the saved Z024 plan on top of
Z023. It is not a completed Z024 runtime profile. The existing Z023 application
remains the latest live profile; CAN1, CAN adapter/actor/safety capability and
application CAN TX remain disabled. Temperature scanning remains enabled in
the inherited validation stack. Its hardware pull-ups were already validated.

## Implemented

- Frozen v2.6.27 scheduler, with only its include path changed: capacities
  4/12/192, active/pending generations, priority classes, reservation/load/abort
  states, required completion event timestamps and epoch behavior.
- Independent production-code differential execution against the bundled,
  unmodified original scheduler: 100,000 transitions including wrap.
- Bounded portable callback/owner transport seam. Three outstanding requests,
  16 events, eight events and at most three sends per service call. Callbacks
  copy by value and use a single nonblocking lock attempt. Contention or overflow
  latches loss and inhibits submissions. Compile-time checks require lock-free
  atomics. No unbounded spin or retry loop exists in production callback code.
- Request identities are monotonically increasing 64-bit cookies. No slot pointer
  is a request identity. Immediate, duplicate, failed and old-epoch callbacks
  cannot complete a newer request. Exhaustion is terminal. The eventual Zephyr
  adapter must preserve this identity through the driver's user-data mechanism.
- A send acceptance increments submission only. Owner processing of a positively
  completed callback increments completion. No required-set credit is possible
  from the advisory-only transport publication API.
- Two-phase begin/readiness/settlement recovery seam; recovery clears all queued
  generations, preserves sticky history and cookie identity, and advances epoch.
  It does not implement a physical controller reset, timing/backoff policy or
  prove that a driver has settled. Those are required adapter responsibilities.
- A missing completion beyond 100 ms inhibits this bench seam and requires
  explicit recovery. This is a provisional diagnostic bound, not a claim of
  parity with the original vehicle bus-off authority policy.
- Exact non-APM `0x68B` current-provenance codec. Freshness <=100 ms; calibration
  quality requires confident record, nonzero ID and finite nonzero uncertainty.
  Invalid encoding is `00 00 00 01 00 00 00 00`, including the oracle's zero age.
- TX and RX queue residence contributes to current freshness. Expired generations
  are discarded; receiver snapshots update the represented age at processing.
  This is explicit migration hardening beyond the original codec call itself.
- Only standard 8-byte `0x68B` DHAB/unavailable messages are accepted by the bench
  seam. Remote, FD, malformed, stale, APM and command messages are rejected.
  RX only copies diagnostics; there is no command decoder or authority mutation.
- The scheduler is linked into the portable core. Its generic API is not a new
  platform raw CAN API, and no application calls it in the current profiles.

Storage: the complete transport object is 16,168 bytes on the tested host ABI.
This is a host measurement, not target RAM/stack qualification. It must be static
and private to the future owner adapter, not allocated on the CAN thread stack.

## Validation

Run `python3 scripts/run_z024_host_validation.py .` from the repository root.
The report and full log are in `docs/migration/evidence/Z024_HOST*`.
The campaign runs directed scheduler/transport/codec tests, 100,000 randomized
oracle scheduler checks, 100,000 differential transitions, ASan/UBSan, GCC
analysis, two-producer callback concurrency (20,000 events), TSan, 21 compiled
behavioral mutants, and the Z023 canonical runner with its inherited chain.
A compiler rejection is not counted as a successful behavioral mutation test.

`check_z024_contract.py` is explicitly a portable checkpoint gate. It is not
registered as a fictitious target/ELF gate. Existing `check_all_contracts.py`
continues to validate the existing runtime profiles. The build manifest likewise
still describes Z023; no Z024 runtime or successful target build is fabricated.

## Blocking evidence and remaining implementation

The pinned Zephyr v4.4.0 CAN source is downloaded and inspected. Its recovery
return-code, mutex and IRQ behavior require adapter mitigation and validation.
CMake 3.31.6 is available for host validation. The target toolchain, west and
Clang remain unavailable here.

The following work is **not implemented**:

1. Verify pinned driver send/callback/stop/recovery/filter/listen-only behavior,
   controller timing and transceiver routing; freeze recovery bounds.
2. Finish the compact/power/logger wire inventory and validate the actual ECU's
   interpretation of missing segments and no-authority encodings. The original
   ECU contract is bundled for review; receiver code has not been validated.
3. Private Zephyr adapter, owner binding, RX filter setup, explicit listener and
   isolated-TX profiles/overlays, actual adapter SIL, copied platform diagnostics.
4. CAN supervision outcome integration, profile-aware capability/manifest/ELF
   gates and the remaining telemetry codecs. Earlier actor IDs stay unchanged.
5. Target builds, RAM/stack/timing review and physical isolated-bus validation.

The portable seam is intentionally the integration boundary for those tasks.
Its test fake is not a substitute for an actual Zephyr driver adapter test.
No full Z024, target, hardware, full-pack, charger, balancing, torque or BMS_OK
qualification is claimed by this checkpoint.
