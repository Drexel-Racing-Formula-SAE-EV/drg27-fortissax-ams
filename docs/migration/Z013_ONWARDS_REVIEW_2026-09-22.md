# Z013–Z024 source and host review — 2026-09-22

Reviewed the current checkpoint, including the earlier Z024 review corrections.
This is a review of selected production paths and cross-stage contracts, not an
exhaustive proof of every path or target/hardware qualification.

## New confirmed defect: Z013 IMD capture data races

The capture callback wrote `volatile` sequence/tuple fields while the worker read
them. CPU/compiler fences did not make those accesses atomic or remove C data
races. `capture_started` was also shared across the worker initialization and
callback boundary without an atomic type. A pthread test calling the unchanged
production publisher and reader reproduced ThreadSanitizer race reports; the
original output is retained in `evidence/Z013_IMD_RACE_BEFORE.log`.

Changed all shared capture fields to C11 atomics with sequentially consistent
accesses, initialized before capture starts. A compile-time requirement rejects
platforms without always-lock-free unsigned-int/boolean atomics. The seqlock still
has one writer and one reader, with at most three read attempts. Concurrent
reinitialization, multiple writers and concurrent access to the decoded output
fields remain outside the API ownership contract. Runtime callers were checked:
the capture callback publishes, the IMD worker decodes, and diagnostic publication
uses its separate atomic snapshot.

The 250-ms expiry, counter saturation, normal/status frequency mapping, invalid
capture rejection, and direct fail-low behavior are unchanged. Updated the
reviewed implementation hashes in `check_imd_capture_contract.py`; the frozen
FreeRTOS oracle hashes remain unchanged. This is a deliberate concurrency
hardening, not a byte-identical oracle port.

The same concurrent production-code test now passes TSan with 100,000 publishes
and reads, checking that successful reads contain one complete input tuple.
Core, production-adapter, UBSan and GCC analyzer tests also pass. This does not
substitute for STM32 interrupt timing and code-generation verification.

## Stage coverage

| Stage | Inspected boundaries and checks |
|---|---|
| Z013 | IMD ISR/worker tuple and decoded diagnostic publication; startup ordering; BMS_OK direct low and fatal-before-halt sequencing. Fixed the race above. |
| Z014 | Heartbeat age/invalid masks, startup grace, feed decisions, stack integrity, ambiguous watchdog startup and terminal feed failures. Retained process-fault versus worker-liveness distinction. |
| Z015 | SPI transfer lengths, CS cleanup, receive invalidation, finite elapsed-time polling; ADC recovery/readback and fan adapter bounds. Original architecture and mutation gates exercised. |
| Z016 | Typed wake/session/protocol boundaries and command-counter invalidation; inherited link/probe tests and negative controls exercised. |
| Z017 | SNAP debt before wire access, UNSNAP cleanup, epoch retries, optional products, cell history and IIR readiness. Earlier corrected cleanup ownership remains intact. |
| Z018 | Mux ownership/ACK checks, AUX reads, partial sample retention, temperature history, temporary OW configuration and mandatory restore. Diagnostic samples stay outside primary history. |
| Z019 | Image withdrawal on interruption, single repair attempt, identity check, history preservation and terminal repair failure. |
| Z020 | Startup MUTE/zero/readback containment and shadow candidate selection; no discharge authority added. |
| Z021 | Ring generation/ticket/counter/cleanup ownership and APM decoding/calibration guards; portable support remains separately scoped from live hardware. |
| Z022 | Current-window locking, dropped samples, publication buffer pinning, source ages and estimator eligibility. Previous estimator overflow correction retained. |
| Z023 | Actor ownership, coherent handoff, sticky history, stale progress/data withdrawal and fault classification. Hot debounce intentionally counts completed 100-ms cycles, matching its recorded oracle. |
| Z024 | Previous recovery, RX expiry, publication age and late-completion fixes retained and regression-tested; driver integration hazards remain unresolved. |

No additional confirmed defect was established in the other inspected paths in
this pass. That statement does not cover every dormant algorithm or interleaving.

## Validation and limitations

- Z015 review invocation: 34/34 executed stages passed, including production
  adapters, source gates, negative controls, null-platform CMake and watchdog
  TSan. Used the existing `--no-sanitizers` option because the earlier full run
  stopped at LeakSanitizer's explicit ptrace incompatibility. The report retains
  its skipped sanitizer/Clang fields; it is not a complete canonical closeout.
- Supplemental UBSan: all ten inherited unit suites plus integrated Z014 safety
  SIL passed. The twelfth supplemental stage is the new IMD TSan test, also passed.
- Z024 campaign: 5/5 stages passed, including its inherited Z023→Z018 campaign,
  21 behavioral mutations and 100,000 frozen-scheduler differential transitions.
- Fresh GCC analysis: all 31 portable production translation units passed.
- LeakSanitizer and Clang validation remain incomplete. The historical failed
  full Z015 attempt remains preserved. Later campaign ASan results do not imply
  leak checking succeeded for the older suites.

Reproduce from the repository root (CMake must be on PATH):

```
python3 scripts/run_z015_host_validation.py . --tsan --no-sanitizers --report docs/migration/evidence/Z013_ONWARDS_Z015_REVIEW.json
python3 scripts/run_z013_review_ubsan.py .
python3 scripts/run_z024_host_validation.py .
python3 scripts/review_portable_analyzer.py .
```

ADC/SPI elapsed-time polling still depends on an advancing timebase; a frozen
clock is not proven bounded by those loops alone. Target clock, interrupt,
watchdog and timing qualification remains required. No assertion of frozen-clock
fault tolerance is made.

Runtime CAN remains disabled and Z024 remains partial. The previously reproduced
pinned-driver recovery return-code hazard still needs adapter mitigation. No
BMS/balance authority or safety evidence was promoted. Temperature scanning stays
enabled in the validation profiles; the user's validated pull-up hardware status
is unchanged. No target build or physical test was performed in this review.
