# Z024 — bounded CAN transport, scheduling and bench telemetry

Status: proposed stage; planning only. Based on the Z023 shadow-supervision
host/SIL closeout. No firmware, hardware enablement or safety capability is
changed by this plan.

## Goal and baseline

Replace the CAN placeholder with an explicitly selected, bounded owner and
copied diagnostics. Port the frozen FreeRTOS scheduler and wire encoding,
then validate receive and transmit behavior on an isolated bench.

The current Zephyr CAN worker has a 100-ms period. CAN1 is disabled in the
board DTS, which declares RX PD0 / TX PD1. These are source declarations,
not new proof of pin routing, transceiver control or bus timing. CAN adapter,
actor and safety-evidence capabilities are currently false.

The selected FreeRTOS source defines a 10-Hz fast publisher, protected period
100 ms, required protected IDs 0x680–0x687, and default detail period 500 ms.
Its scheduler has critical/protected/detail storage limits of 4/12/192 frames
per generation, active and pending generations, and controller-epoch tokens.
Do not flatten that into a FIFO or treat submission as completed transmission.

Z023 target/profile builds, the unavailable CMake/Clang gates, and physical
qualification remain open. Z024 source work can proceed against its frozen
host baseline, but must carry those gaps into its evidence record.

## Scope boundaries

Included: CAN1 adapter, portable scheduler, bounded RX processing, copied
measurement/fault telemetry, controller fault and recovery handling, owner
progress, contracts, SIL and explicit bench profiles.

Excluded: charger commands (including a purported harmless command that changes
charger state), charge/AIR control, ECU torque permission, SoP authority,
balancing, BMS_OK assertion, tuning writes and full-ring/APM activation.
Critical scheduling semantics are tested synthetically without introducing a
live charger-control path. Detailed logger/tuning streams may remain disabled
when their producers or wire-validity rules are not ready.

Temperature scanning remains enabled. Its hardware/pull-ups are already
validated; remaining temperature tests concern Zephyr migration behavior.

## Z24-A — freeze the oracle and wire contract

Read the combined `canbus_task.c`, `canbus.c`, `can_tx_scheduler.c`, logger,
build-profile and ECU-facing definitions. Record source hashes and selected
macros. Initial anchors from the available v2.6.27 tree:

| File | SHA-256 |
|---|---|
| can_tx_scheduler.c | 9fe7b7ad86bac0c888e2e58b19b5686cf0e25e1cb7ce9347b15ed4ca7f291503 |
| canbus_task.c | 75c883321c438d00551afb012b0d566dae2c9f3e9e148c7bc09d09eb08b8564e |

Produce a per-message table: ID, standard/extended format, DLC, period, byte
order, scaling, rounding/saturation, invalid sentinel, validity bits, sequence,
source freshness, and whether the receiving ECU interprets it as permission.
Freeze bitrate, sample point, clock assumptions, transceiver enable/standby
wiring, filter resources and bus-off settings before enabling the peripheral.
Check the local pinned Zephyr revision and driver implementation for actual
send, callback, filter and recovery semantics; do not infer them from API names.

Resolve the single-SMB compatibility issue explicitly. Missing segments stay
invalid. Never label 15-cell voltage as a valid 75-cell pack voltage, duplicate
the populated segment, or advertise advisory SoC as qualified SoH/SoP. Verify
that the ECU understands invalid/no-authority encodings. If it does not, keep
those frames off and restrict the image to receive-only or an isolated test
receiver until a separate wire-version change is agreed. No silent protocol edit.

## Z24-B — portable scheduler and codecs

Port generation supersession, priority classes, required/advisory distinction,
deadlines, reservations, loaded/aborting/completed/discarded states and recovery
epochs. Retain the oracle's token identity and completion-event timestamps.

Use bounded storage. Keep original capacities for initial parity testing;
measure RAM before choosing profile-specific reductions. Any reduction requires
an explicit reviewed scope restriction, overflow tests and updated contracts.
Define wrap/exhaustion behavior for generation and request IDs. Saturating
diagnostic counts must not become freshness tokens.

Build each generation from one immutable copied measurement plus independently
timestamped fault/supervisor inputs. Preserve provenance; do not claim simultaneous
current, temperature and voltage sampling. Invalid/stale sources produce the
frozen invalid representation. Hardware comparator warnings remain separate
from software electrical faults.

Deliver independent codecs and a scheduler that compile without Zephyr/HAL.
Differentially compare payload bytes and scheduler traces with the frozen oracle.

## Z24-C — private Zephyr adapter

Bind the sole CAN owner before threads start. Use the existing Zephyr CAN driver
where its verified behavior meets the contract. Keep controller handles and
transport buffers private; expose copied status and typed owner operations.

Callbacks may run before the send function returns. Reserve token ownership
before submission and make callback/submission-failure resolution exactly once.
Maintain a fixed outstanding-token table with controller epoch, generation and
frame identity. Old, duplicate, late and abort-racing callbacks cannot complete
a reused slot or credit a newer generation.

Callbacks perform bounded event capture only: no blocking, frame generation,
recursive submission or application-state mutation. An event-queue overflow
invalidates completion certainty, inhibits TX and initiates explicit recovery;
it must not silently drop a completion and reuse its token. Copy RX payloads
before callback storage expires. Account for filter-installation partial failure.

Bound all sends, locks, RX draining and recovery operations. Demonstrate that
an unavailable mailbox cannot starve current, ADBMS or the supervisor. Preserve
the absolute 100-ms owner schedule with skipped releases and no catch-up bursts.
Record whether latency requires bounded completion-driven servicing between
periodic publication releases; do not add an unbounded drain loop.

## Z24-D — recovery and receive containment

Distinguish error-active/passive, bus-off, submission error, failed completion,
RX overrun, event loss and terminal adapter failure. Freeze retry budget,
backoff and terminal behavior from the oracle plus driver guarantees.

On bus-off or uncertain controller state: inhibit submissions, invalidate
outstanding ownership, advance controller epoch, discard obsolete generations,
and preserve diagnostic history. Recovery requires positively established
controller readiness. Do not assume stopping the controller cancels every
callback. Fresh telemetry generations follow recovery; no stale replay burst.

Allowlist RX IDs/formats/DLCs. Reject malformed, remote, unexpected and stale
messages before decoding. Z024 RX cannot change charge, balance, AIR, calibration,
tuning or BMS authority. Hostile RX load receives a fixed processing budget.

## Z24-E — supervision, profiles and evidence

Add explicit Z024 profiles depending on the Z023 validation stack:

1. Listen-only bench validation: no application TX and verify the driver mode's
   actual ACK behavior; do not call ordinary receive mode electrically silent.
2. Isolated bench TX validation: only the frozen no-authority telemetry allowlist.

Keep CAN1 disabled in earlier profiles. Select board enablement through explicit
configuration/overlay handling, preserving other routes. Update capability,
architecture, target/ELF and manifest gates together.

Extend Z023 with a distinct CAN outcome/progress record without renumbering
existing actor identities. Preserve its 2000-ms watchdog timeout and the existing
unseen-startup rules. A completed bounded RX/error/TX-service cycle can prove
worker progress; it does not prove successful delivery. Submission, successful
controller completion and complete required-set delivery are separate metrics.
Listener profiles have no TX-delivery claim.

Supervisor fault disposition precedes shadow progress acceptance. Bus-off
handling may be live while the link remains faulted; a cached error cannot renew
progress. CAN safety evidence stays false until separate qualification. Do not
remove CAN from the oracle required mask or imply full coverage when CURRENT,
ADBMS or TEMP evidence is also unqualified.

## Z24-F — validation and negative controls

- Golden payload tests: every allowed ID, valid/invalid inputs, exact boundaries,
  endianness, counter wrap, absent SMBs and stale independent sources.
- Differential scheduler traces: priority inversion, required-set deadlines,
  generation replacement, detail shedding, reservation rollback and wrap.
- Actual production-adapter SIL: immediate callbacks, delayed/out-of-order and
  duplicate callbacks, send failure, busy mailbox, callback overflow, abort races,
  bus-off during send, partial filter setup and recovery-epoch replay.
- RX floods, malformed formats/DLCs, prohibited commands and bounded drain tests.
- Combined current/ADBMS/supervisor workload: mutex contention, retained samples,
  scan cadence, no catch-up and no heartbeat from enqueued/cached work.
- Compile and run behavioral mutants for token reuse, forged completion,
  dropped queue-overflow handling, stale generation, mailbox blocking, invalid
  encoding, authority promotion and weakened watchdog requirements.
- Run Z023 and inherited regressions, available sanitizers/analyzers, and the
  target matrix for old profiles plus both Z024 modes. Report missing tools.

Bench validation records real bitrate/timing, TX completion versus analyzer
capture, missing ACK, controlled bus-off/recovery, RX flood bounds, required-set
latency, priority/scheduling behavior and physical BMS_OK low. Do not disturb an
operating vehicle bus to create faults; use an isolated receiver/load setup.

## Exit criteria and deliverables

Separate source/host, target, physical and safety-evidence completion. Source/host
completion requires reproducible production-code tests and negative controls;
target completion requires actual builds, ELF/config checks and resource review;
physical completion requires captured timing/fault evidence. None automatically
grants vehicle or charger authority.

Deliver the frozen wire/oracle table, portable scheduler/codecs, bounded adapter,
RX decoder, copied diagnostics, two explicit profiles, supervision integration,
contracts/mutations, canonical runner, target/bench checklist and independently
verified ZIP with source hashes. Proposed implementation areas:
`lib/ams_core/can/`, `drivers/ams/can_zephyr.c`,
`include/ams_platform/can.h`, `check_z024_contract.py`,
`check_z024_mutations.py` and `run_z024_host_validation.py`.

Implementation order: oracle/wire freeze → portable models → callback/epoch SIL
→ receive-only adapter → isolated telemetry TX → supervisor integration → full
regression/package → target/physical qualification. Resolve wire validity and
driver completion semantics before live TX, rather than carrying assumptions
into later gates.
