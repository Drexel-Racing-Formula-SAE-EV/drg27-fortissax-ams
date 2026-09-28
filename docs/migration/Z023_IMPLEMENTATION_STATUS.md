# Z023 shadow-profile host/SIL closeout — 2026-09-16

Status: **shadow-profile implementation and host/SIL campaign complete**. This is
not target-qualified, hardware-qualified, or permission to assert BMS_OK.
Use `app/z023_supervision_validation.conf` with the existing board build.
The profile depends on Z022/Z020/Z019/Z018. Earlier profiles retain their
existing dispatch. Temperature scanning remains enabled; its hardware and
pull-ups were previously validated by the owner.

## Implemented

- Portable CURRENT/ADBMS/TEMP completion records with 64-bit monotonic sequence,
  rejection of replay/future/late handoff, fail-closed sequence exhaustion,
  sticky fault history and copied decisions. Exhaustion does not wrap into a
  new epoch. Startup is stale at 3000 ms; seen actors are stale only above
  200/3000/3000 ms. Normal uint32 clock wrap is supported; clock epochs must
  not be left unobserved for an entire uint32 period.
- Explicit current/ADBMS/supervisor owner binding before application threads
  start; ISR and wrong-owner rejection before acquisition begins.
- Current completion follows actual ADC attempt, sensor conversion, the
  existing PRECHARGE fault update, current-window disposition and diagnostics.
  Invalid/unqualified data and mutex/publication failure remain distinguishable
  from task progress. Calibration is neither fabricated nor auto-zeroed.
- Monitor completion follows real initialization/acquisition/recovery, cleanup
  classification and publication disposition. Cached terminal returns cannot
  renew progress. The first terminal transition is retained; subsequent cached
  returns eventually become progress-stale.
- TEMP requires eight distinct positions in one recovery generation. Received,
  attempted-but-failed and suppressed sensor masks are separate. A received ADC
  code is not necessarily a valid temperature. Completed masks and generation
  are exposed in the copied supervisor decision. Recovery discards partial
  coverage. No extra scan/retry or scheduling catch-up was added.
- Compact single-segment raw-C voltage and temperature fault policy: electrical
  thresholds are separate from acquisition plausibility, fault latches survive
  recovery/good samples, hot debounce counts completed 100-ms monitor updates,
  and Status-D remains advisory. No pack-validity claim is made.
- The safety thread polls the decision and reinforces fail-low. Progress
  timestamps are **shadow heartbeat evidence only**: CURRENT/ADBMS/TEMP safety
  capability flags remain false and no new hardware-watchdog feed credit is
  granted. CAN remains in the original required mask. Estimator remains advisory.
- Profile, source/ELF contract, source negative controls, build-manifest fields,
  production adapters/hooks, sanitizers, GCC analysis, policy differential test,
  concurrent handoff test and inherited regression runner.

## Synchronization and ownership

ADC and SPI work remains outside the supervision spinlock. The current-window
2-ms mutex and lock-before-boundary timestamp order are unchanged. The monitor
snapshot lock is released before staging supervision. Single-owner staging is
private; one short spinlock transaction commits publication disposition,
fault-policy copies and progress records. The supervisor takes the same lock,
reads evaluation time, commits fault history and consumes each sequence once.
No supervisor waits on ADC/SPI or the current-window mutex. Current fault
records are independently timestamped; they are not relabeled as the voltage
epoch. A last-value mailbox coalesces progress but retains all fault categories.

## Oracle freeze and scope

The unmodified v2.6.27 policy files are bundled under
`tests/unit/z023/oracle/`; only `ext_drivers/accumulator.h` there is a host input
seam, explicitly configured for one SMB, 15 cells and 24 temperatures.

| Oracle file | SHA-256 |
|---|---|
| voltage_fault.c | c6c09eb8ec78e14d2f45787fd44fd87c90d767dc29d6ad70d36772d546f5456f |
| voltage_fault.h | e9f35bc08e7213774e1bd6afcdac0ba2372b57adfb6bce5285e8209162b6da8a |
| temperature_fault.c | 61e08fe751237789927572dc4367b3751251fd77d3f1aec95cbc55b6af4b9ac5 |
| temperature_fault.h | 6507af083b8f97020fee0313c9d1aec1b8218d7df28c2308afdcd264f9cffb05 |
| adbms_task.c | fdbe4ff7f8580c448184b24edc040c1e10f5be50945fc4fa7fdb73d7e8f760e6 |
| current_task.c | b0f26b71426af014f94fe2803f6654cf9f2bda0a8af68ed1612ff086593ab0f2 |

Voltage warning/charge stop/hard/severe: 4150/4180/4200/4250 mV.
UV warning/soft/hard/severe: 3000/2800/2500/2300 mV. Severe OV, severe UV,
hard OV, hard UV retain that priority. After a valid scan, two read failures
are pending and the third confirms; all failed reads still inhibit this bench.
Hardware comparator disagreement uses the oracle's 20-mV margin and masks C16.
Hardware UV at 3 V is not a software hard-UV trip.

Temperature charge-stop limits: <=0 C or >=45 C; fan-max warning >=50 C;
hard >=60 C and severe >=65 C each require 2000 ms. Tier changes and invalid
input reset pending debounce. Only completed owner cycles add 100 ms, including
retained but still usable temperatures, matching the selected task call site.
No charge-control or fan command is issued by this new diagnostic policy.

Current reuses the previously ported policy unchanged: PRECHARGE warning 0.8 A,
trip 1.2 A for 200 ms, fast trip 2 A for 40 ms; ADC invalidity confirmation
after 500 ms of completed 20-ms updates. Its polarity/calibration provenance is
not upgraded by this stage.

The compact policy does not copy every oracle diagnostic reason/count. It adds
explicit raw validity/age checks and confines the policy to the populated
segment. Unavailable startup data is confirmed unavailable, with no full-pack
readiness inferred. The original plan remains the governing completion scope.

## Evidence and remaining gates

Run `python scripts/run_z023_host_validation.py .` for the reproducible campaign.
`docs/migration/evidence/Z023_HOST_REPORT.json` records the exact source hashes
and which stages ran. Source negative controls test checker sensitivity, not a
claim that every mutation was behaviorally executed. Policy differential testing
covers 100,000 sequences; the concurrent production supervision adapter test
performs 50,000 current and 10,000 monitor handoffs with real host atomics.

The actual bounded Z023 safety-thread workload is now `ams_z023_safety_cycle`.
The real safety thread calls this function before watchdog evaluation; the
source gate verifies that ordering. Integration SIL links the same function,
production current worker, current window/publication, sensor/fault model and
supervision adapter. Only ADC/monitor inputs, kernel primitives and physical
fail-low writes are replaced by host seams. A reader-pin fault injection uses
test-only access to the production store, without exporting an ownership API.
The entire Zephyr scheduler and STM32 peripherals are not emulated by this test.

Directed integration covers healthy scanning, current invalidity, current-lock
failure, dropped pinned publication, repeated publication, stalled owners,
rejected/ISR handoff, delayed monitor handoff and interrupted/recovered scan
coverage. Actual monitor-adapter tests also cover cached terminal returns and
both optional diagnostics compiled together. Sixteen compile-successful unsafe
mutations must fail behavioral assertions; these are separate from the source
negative controls. The concurrent production adapter also passes ThreadSanitizer.

Two additional defects were corrected in this closeout:

- Late/future/replayed records could copy policy details independently of their
  rejected record sequence. Details and scan metadata now move only with an
  accepted corresponding record; rejected-record fault history remains sticky.
- A stalled producer could leave copied validity flags asserted. The supervisor
  now expires them against oldest sample timestamps and worker freshness, with
  the data-stale fault retained across repeated evaluations.

Remaining gates (not host-SIL passes):

1. Target/profile matrix and stack/RAM checks; west, CMake and Clang were not
   available in this execution environment. No target build was performed.
   The CMake-based null-platform gate was attempted and blocked by missing
   CMake. A separate strict C11 syntax check passed for every portable core
   translation unit; it is not a replacement for that CMake/link gate.
2. Physical Zephyr fault/timing tests and actor-by-actor evidence qualification.
   Only then consider changing safety-evidence flags. Z021 mixed-ring/full-pack
   and CAN remain separate outstanding dependencies.

All output authority remains disabled. There is no latch-reset API in the new
shadow policy: its latches clear only with boot/reinitialization, not recovery.
No blanket full-stage or vehicle-readiness claim is made. Inherited reports
distinguish executed regression from historical evidence.
