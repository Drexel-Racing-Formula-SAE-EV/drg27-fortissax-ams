# DRG27 AMS — Z-016 ADBMS6822 / isoSPI port plan

Date: 2026-09-08. Status: **planning only; no Z-016 firmware implemented or tested in this review.**

Behavioral oracle: DER26 AMS **v2.6.27 / firmware 0.5.30**. Destination: `C:\DER_AMS\git\revert\DER27-AMS-zephyr`, Zephyr revision `v4.4.0`.

**Decision:** Z-016 establishes the single-owner isoSPI link: checked wake/cold-wake, bounded timing, session handling, packet integrity, and a restricted read-only link probe. Z-017 begins monitor initialization and cell acquisition. The entire 6830 driver, accumulator, and ADBMS task will not be copied into Z-016.

The known Z-015 linker-map substring checker defect is deferred at the user's request. It does not prevent planning. It remains recorded as an unresolved target-contract gate; it is not grounds for claiming Z-015 target-green.

## 1. Evidence baseline and work already completed

This plan combines the supplied migration handoff, the detailed v0.5.30 plan inside the reference ZIP, direct examination of the reference protocol/runtime paths, and the current Z-015 source. Historical test reports are retained evidence, not new test executions.

| Area | Current evidence and consequence for Z-016 |
| --- | --- |
| Z-001–Z-005 foundation | Custom STM32F767 board, static thread topology, fail-low PE0 path, timing/heartbeat infrastructure exist. Reuse these. AIR remains explicitly represented with its existing enablement policy. |
| Z-006–Z-010 portable behavior | Measurement store, current-window integration, estimator, SoP/SoH/fuse and supporting contracts exist. Their presence does not mean all acquisition/publishing actors are connected. Do not alter their algorithms in Z-016. |
| Z-011 current path | Current sensor/fault core and private recoverable ADC1/ADC2 backend exist. Generic ADC1/2/3 ownership is excluded because reset is shared. Current actor/evidence capabilities are still false in the examined Kconfig. |
| Z-012 fan | PWM adapter and actor exist; output-only TIM3/4/5 IRQs are disabled and pending-cleared. Preserve exact period/duty behavior. Physical validation is a separate claim. |
| Z-013 IMD | TIM2 capture and IMD actor exist. Do not take TIM2 for ADBMS microsecond timing. |
| Z-014 watchdog | AMS-owned liveness/integrity policy, heartbeat concurrency, stack checks and IWDG validation profiles exist. Physical/process faults do not independently stop feeding a healthy system. Full oracle heartbeat coverage is not yet claimed. |
| Z-015 SPI6 | Private synchronous bounded LL backend, manual PE2/PE4 CS, Mode 3, 421,875 Hz, 500 ms transfer timeout, 512-byte limit, 0xFF read dummy bytes, RCC recovery and NVIC quiescence exist. No stock Zephyr SPI owner, DMA, async or RTIO. |
| Z-015 runtime boundary | Startup initializes the adapter without clocks or wake. ADBMS is still a placeholder; raw transfer entrypoints are deliberately unused in the final image. |
| Host evidence | Prior NVIC-fixed closeout reports 54/54 host/source/SIL stages, 18 Z-014 mutations and 47 Z-015 mutations. Later checker-only fixes have their own focused regression evidence; the complete campaign was not rerun for the ADC Kconfig correction. |
| User target evidence | Base and IWDG builds have linked successfully. Latest supplied contract runs pass ADC, fan, IMD, capability and watchdog checks before stopping in the SPI checker. The remaining source-only checks and final manifest stage have not been shown to complete. |

The handoff's statements that Zephyr has not started and the repository is empty are historical. Its roadmap and safety invariants remain useful; its stopping point is superseded by the current source and build logs. Some architecture prose also predates the private ADC/fan hardware exceptions. Update that prose when implementing Z-016 rather than using stale descriptions to override current code.

### Source identity

The local working Z-015 files were compared with the latest saved ADC-Kconfig-fixed ZIP; all ZIP member contents matched.

| Input | SHA-256 |
| --- | --- |
| `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06(9).zip` | `9fb9f98f62a6ecee259ad9e3d3c3ad3afbd7c96714d5fc82cacf4ceba75563d0` |
| `DER27-AMS-Z015-target-ADC-Kconfig-fix-2026-09-08-full-repo.zip` | `94e4b7460aaab8aeeb925fc191c8a138e9a40ee1307b073060461fb49fb59d82` |
| `DRG27_Fortissax_AMS_Zephyr_Migration_Handoff_2026-09-06(5).md` | `ceff59d032a9f2f57440f8cbb320ec528bc8b66234461d4451a652079014af3c` |

Before implementation, retain these identities and capture the actual resolved Zephyr and HAL-module commit SHAs from the user's west workspace. The examined manifest pins the tag `v4.4.0`, not a literal commit SHA.

## 2. What “ADBMS6822 driver” means here

The ADBMS6822 is the SPI-to-isoSPI transceiver. The examined oracle has no separate `adbms6822.c`; the host-side link behavior is embedded in `adbms6830.c` and shared helpers. RDSID, configuration frames and their PEC are monitor protocol, not a newly invented 6822 register interface. Analog Devices describes the 6822 as a dual transceiver translating the serial interface across isolation. [ADI ADBMS6822 product description](https://www.analog.com/en/products/adbms6822.html).

Z-016 therefore extracts a **link/session foundation plus the minimum 6830 packet support required to test it**. It does not add LPCM, transceiver interrupt handling, new pins, faster communications or a generic multi-board driver framework.

| Include in Z-016 | Keep in later stages |
| --- | --- |
| Normal and cold wake, checked cleanup, A/B selection | Full 6830 initialization, SRST/config writes and startup POST: Z-017 |
| Explicit topology metadata and bounded storage | Automatic chain discovery or runtime direction failover: no new behavior during parity work |
| PEC15 command framing, PEC10 packet validation, command-counter tracking | Cell conversions and C/S/AVG8/IIR products: Z-017/Z-018 |
| Pure 48-bit write-frame packing and command-effect tests on host | Live configuration/PWM/COMM writes: their later feature stages |
| Session guard decisions and coherent-epoch invalidation foundations | Real SNAP/UNSNAP acquisition orchestration: Z-017 |
| Single ADBMS owner and bounded request/result lifetime | General service CLI: Z-032; each earlier diagnostic must still use the owner |
| Restricted RDSID and RDCFGA probe through the real transport | AUX/mux/temp/redundancy: Z-018; protocol recovery: Z-019 |
| Classified failures and transport-recovery propagation | Balancing: Z-020; APM/2950: Z-021; publication: Z-022 |

**Z-016 completion is not M5 ADBMS parity.** M5 spans the later acquisition, diagnostics, recovery, balancing and APM work as well.

## 3. Frozen topology and transaction contracts

### 3.1 Physical chain versus logical device set

The oracle validates `num_ics`, `physical_chain_count`, `ics_capacity`, selected string and write string independently. Wake uses the physical count. A register read requests packets for the logical addressed subset. Preserve that distinction.

| Topology | Logical SMBs | Physical devices to wake | SMB access | Z-016 treatment |
| --- | ---: | ---: | --- | --- |
| Current five-SMB bench-validation scope | 5 | 5 | String A | Primary probe profile; no APM traffic |
| Isolated single-SMB validation | 1 | 1 | String B | Explicit alternate profile, never a silent fallback |
| Eventual mixed ring | 5 | 6 | SMB subset from A; APM from B | Host topology fixtures now; live mixed-device protocol deferred to Z-021 |

The default oracle `BENCH` macro path can select one SMB on B, while its normal `BENCH_VALIDATION` path selects five on A. Do not copy the first default encountered or trust nearby historical one-SMB comments. Freeze the resolved intended profile in Kconfig and the generated build manifest.

Preserve the reference bounds of 16 tracked ICs and 16 physical devices in reusable validation logic, while allocating only the reviewed profile's runtime result storage. Reject zero counts, physical count below logical count, capacity overflow and invalid direction before touching GPIO.

Record receive order as **wire order plus access direction**. Do not assume the same index identifies the same SMB from both ends, reverse all arrays indiscriminately, or shrink the expected device set when a link is broken. A read of five valid packets proves that addressed prefix responded; it does not independently prove that a sixth physical device is absent. Full-ring topology validation requires the later endpoint/identity evidence.

PE4 remains CS_B. The accepted pin assignment and prior five-SMB temperature validation are not reopened by this plan.

### 3.2 Wake and framing

| Behavior | Exact reference requirement |
| --- | --- |
| Normal wake | For each physical device: selected CS low for 1,000 µs, then high for 1,000 µs; no SPI clocks generated by the wake primitive |
| Cold wake | Two complete normal-wake pulse trains, then 5,000 µs cooperative settling |
| Command frame | Two opcode bytes plus two PEC15 bytes, high PEC byte first |
| Register read | One CS-low full-duplex transaction: 4-byte command prefix, then `8 × logical_count` clocked response bytes using 0xFF TX dummy bytes |
| Read packet | Six data bytes, then six command-counter bits plus ten PEC bits across the final two bytes |
| Register write packing | 4-byte prefix, then six data bytes plus PEC10 per IC in reverse logical order; live writes must use the configured write direction |
| Transfer capacity | Check prefix plus response together against 512 bytes before transfer |
| Read cleanup | Failed result is invalid even if stale bytes remain in an internal workspace; externally exposed payload must not retain a previous valid result |

Preserve helper-specific wake behavior: `cmd_checked()` does not automatically wake; its caller owns that prerequisite. `wr48_checked()` explicitly wakes even when another helper might otherwise reuse a session. `rd48_checked()` selects standalone wake or guarded session use. A universal “wake before every SPI call” wrapper would not preserve the oracle.

Nominal requested normal-wake delays are 2/10/12 ms for 1/5/6 physical devices; cold-wake delays are 9/25/29 ms including settling. These exclude GPIO/software overhead and preemption and are not maximum wall-clock guarantees.

A five-SMB register read is 44 clocked bytes, about 0.835 ms at 421,875 Hz. Standalone wake plus read requests roughly 10.835 ms before overhead. This explains why preserving awake sessions matters; repeatedly inserting full-chain wake delays would change acquisition timing materially.

## 4. Source decomposition and target architecture

Keep the existing downward dependency boundary. Proposed filenames below are implementation targets, not files already delivered.

| Destination | Responsibility |
| --- | --- |
| `lib/ams_core/adbms/ams_adbms_pec.c` | Exact shared PEC calculation with length-aware entrypoints |
| `lib/ams_core/adbms/ams_adbms_frame.c` | Command/write encoding, read-packet decoding, bounds and per-packet result masks |
| `lib/ams_core/adbms/ams_adbms_session.c` | Explicit state transitions for sessions, guard expiry, counter predictions and invalidation; receives time/events as values |
| `lib/ams_core/include/ams_core/ams_adbms_link.h` | Neutral enums, topology, fixed-size state and result types; no HAL or Zephyr types |
| `drivers/ams/adbms_link.c` | Owner-only protocol executor binding pure decisions to the private transfer/wake/timing operations |
| `drivers/ams/adbms_spi_stm32.c` and its private interface | Existing SPI6 ownership plus narrowly scoped wake support; sole GPIO/SPI hardware owner |
| `drivers/ams/adbms_time_zephyr.c` | Checked short delay, cooperative wait and wrap-safe time adaptation; audit before selecting exact implementation |
| `include/ams_platform/adbms_link.h` | Typed owner-facing link operations and status; no raw opcode, arbitrary TX buffer or naked CS API |
| `app/src/ams_adbms_owner.c` | One owner execution context, static requests/results, scheduling and runtime accounting |
| `app/src/ams_threads.c` | Existing static thread allocation/priority registration; replace only the ADBMS placeholder body |

The owner invokes typed platform operations; the driver executor calls private SPI. Pure codecs/session decisions do not invoke Zephyr, STM32, clocks, GPIO or application policy. Fake time/transport can be supplied to the executor in host tests without pretending a HAL implementation is Zephyr.

Do not move the complete 8,100-line 6830 file into `ams_core`. It contains hardware access, diagnostics, mutating commands, global buffers and device-product policy. Extract reviewed units with provenance. Retain the oracle's math/bit operations; change the platform seam and explicit ownership, not protocol semantics for style.

Do not globally replace `HAL_OK` with zero. Give transport, packet integrity and session state separate neutral results, then map them explicitly at the seam.

## 5. Owner and request/result lifetime

The existing transport atomic READY→ACTIVE transition detects overlapping calls. It does **not** prove that two different threads cannot take turns using SPI. Z-016 must establish both static call restrictions and runtime owner identity before adding the first caller.

1. Reuse the existing preemptible ADBMS thread: priority 3, allocated stack 8 KiB, below safety/current and above CAN/estimator. Do not add a second bus thread.
2. Bind the platform executor to that thread once from the owner entry. Reject ISR use, other-thread access, reentry and rebinding. Keep registration startup-only and source-restricted; do not expose a public “become owner” service call.
3. Hold logical ownership across request validation, frame preparation, wake, transfer, decode and cleanup. Do not recreate the broad recursive mutex architecture.
4. Begin with four statically allocated request/result slots and one executing request. Four is a proposed bounded service capacity, not a borrowed safety threshold. Record actual RAM cost in the build report.
5. Use copied typed arguments: operation enum, approved direction, request ID/generation and expiry. No caller-owned TX/RX buffers, stack pointers, arbitrary opcodes or callback pointers in queued requests.
6. Store results in the same persistent slot. Publish completion only after data/status are complete; use one short synchronization operation for each slot-state transition. Never hold that lock during SPI or delays.
7. A client collects a copied result using the matching slot generation. Queue full returns a busy result. Expired queued work is rejected before electrical activity.
8. Client timeout does not cancel active hardware work and does not free an active slot. Mark it detached; the owner completes/cleans the operation and reclaims it. Protect detach-versus-completion and slot reuse with the same state synchronization.
9. Completed uncollected results need a bounded retention/reclaim policy. An expired generation must return “result expired,” never the next request's bytes. Do not reclaim ACTIVE storage on a client deadline.

Minimal request progression is FREE → QUEUED → ACTIVE → DONE → FREE. Expired queued requests and detached active requests have explicit cleanup transitions. An implementation can use a static slot array and fixed index queue; it does not require a generic RPC framework.

The initial operations are `LINK_STATUS`, `WAKE`, `COLD_WAKE`, `READ_SID` and `READ_CFGA`. Electrical operations are enabled only in the dedicated probe profile. General shell migration remains deferred. A compile-time probe runner may submit the same typed requests; it must not bypass the owner merely because it is a test.

Use the existing 100 ms owner release as the initial dispatch opportunity, servicing at most one queued request per release. Preserve the established absolute-release/skip-missed-period policy and record overruns; never drain all queued requests as a recovery burst. The eventual acquisition job has precedence over routine diagnostics, and urgent interruption is independent of queue capacity. This dispatch interval does not replace the microsecond session timing inside an operation.

Put frame workspaces and outstanding results in the explicit static owner/link context. Share one bounded workspace across synchronous operations and copy completed data into the owning result slot before reuse. Do not import all four 512-byte global oracle buffers or its large diagnostic arrays without need, and do not create large automatic arrays on the owner stack. Include the resulting `.bss` and stack delta in closeout evidence.

## 6. Wake and time adaptation

### 6.1 Extend the existing hardware owner

The present private SPI interface has write and write/read only. Wake requires a deliberate addition, not a fabricated zero-length transfer.

Add one checked private wake operation that validates string/count, acquires the same active-operation exclusion as transfers, keeps SPI disabled, emits the requested CS pulse train and attempts to restore both CS lines high on every exit. The GPIO objects stay in `adbms_spi_stm32.c`; a second file must not independently configure PE2/PE4.

A failed CS write or unverified cleanup is reported, not hidden by a void function. The reference cold-wake entrypoint returns void and ignores the final settle result; the port's checked wrapper must propagate those failures. This is an intentional error-reporting improvement, with unchanged successful pulse sequence, not a claim of identical behavior on that formerly unreported failure.

No interrupt masking or scheduler lock spans a wake pulse, transfer or settling delay. Safety/current remain able to preempt the owner. Busy-wait is allowed for the reference's short electrical intervals; long conversion/settling waits remain cooperative.

### 6.2 Time-source admission gate

The oracle short delay has both a timer condition and an independent spin ceiling. A mechanical replacement with an unchecked busy-wait would lose its stopped-timer failure path.

Implement a small checked time adapter only after auditing the **actual pinned target** clock/cycle implementation:

- Short-delay candidate: validated Zephyr hardware-cycle reads, unsigned cycle delta, 64-bit multiplication for µs-to-cycle conversion, upward rounding, and an independent finite iteration ceiling. Preserve conservative oracle spin-ceiling constants initially, then verify optimized target behavior; do not silently reduce them.
- Resolve the actual cycle frequency through the target-supported interface. Do not assume the cycle counter always runs at SYSCLK or invent a Kconfig capability to make a host fake compile.
- Session clock: monotonic microseconds with explicit validity. Prefer a supported monotonic 64-bit source if the pinned target supplies one. Otherwise extend cycle deltas in the sole owner and use coarse uptime to detect idle intervals too long to disambiguate wraps. Ambiguous time invalidates the session and requires fresh wake; it is not converted into a fresh timestamp.
- Never divide a raw wrapping 32-bit cycle count before extending it. The reference explicitly fixes that error: at 216 MHz a 32-bit raw cycle counter wraps in about 19.9 seconds.
- Treat time zero as a valid epoch when accompanied by an explicit validity flag. The reference uses zero sentinels; record this representational improvement and test startup/wrap cases rather than importing an accidental “guard disabled at zero” condition.
- Cooperative waits preserve one-millisecond chunks, interruption checks and a checked short remainder. Do not replace them with one long uninterruptible sleep. Record actual elapsed time for the next session guard.
- Keep long-wait cancellation at a CS-high boundary. Do not release ownership or admit another command inside a partially completed logical request.

The exact v4.4 timer source could not be retrieved in this review environment. Therefore no particular new cycle/time API implementation is claimed target-audited here. This is a narrowly defined first implementation gate, using the existing local west checkout. Do not allocate TIM2/3/4/5 as a shortcut; they already serve IMD/fans.

Every request must have a finite number of pulse/transfer/cleanup steps. Preserve the existing 500 ms transfer deadline. Account separately for wake, transfer timeout, reset/reconfiguration and cleanup; a 500 ms transaction bound is not a 500 ms complete-request bound. No automatic retry loop is introduced in Z-016.

Stop the remaining probe sequence after a failed step. Calculate the worst request path from its actual step count, including how many successful transfers can precede a failure. A 500 ms polling failure at ADBMS priority can delay lower-priority CAN/estimator work even though safety/current can preempt it. Z-016 must measure/report that interference; it must not claim that 10 Hz acquisition or later full watchdog coverage is qualified by the nominal sub-millisecond wire time. Likewise, checking an urgent flag every 1 ms during cooperative waits does not imply 1 ms urgent-MUTE latency through an active SPI transaction.

## 7. Session, integrity and failure semantics

### 7.1 Awake sessions

Port `session_open`, `session_close`, `session_note_activity`, `session_require_awake` and owned read-session behavior as an explicit state model.

- Preserve the 3,000 µs guard. A gap **less than** 3,000 µs continues; equality or greater expires it.
- Non-coherent session expiry may perform the reference checked re-wake before continuing.
- Expiry during a coherent snapshot must invalidate/restart the whole snapshot. It must not splice data from two wake epochs. Z-016 tests this as pure state behavior; live SNAP orchestration comes later.
- Update activity from successful electrical operations at the reference boundary. Track session ID, counts, gap/duration maxima and wake/re-wake counts with the reference wrap/saturation rules.
- The guard is advisory against preemption, not proof of a maximum hardware idle gap. PEC/counter evidence and later coherent-product validation remain necessary after the check. Do not “solve” the check/act race by preventing safety interrupts.
- Direction change, ambiguous transfer outcome, uncertain time and transport recovery invalidate the affected logical session. No stale “awake” token survives a reset or direction change.

Keep a link/session generation separate from request IDs. Both host directions share one owner and may reach the same physical ring; they are not independent protocol universes. An old request result or APM awake token cannot be reused after a generation change. Later mixed-ring command effects must update all affected recipients under that same owner.

### 7.2 PEC and counters

Port the reference PEC15 seed/table and PEC10 algorithm. PEC15 output includes the zero low bit; receive PEC10 includes the six counter bits. Write PEC10 uses zero counter bits and must not read beyond the six data bytes. Length-aware wrappers must reject truncated packets before decoding `data[len]`.

Counter tracking is per addressed IC and must remain active with debug capture disabled:

- Reset baseline is zero; normal increment is 0→1, 1→2 … 63→1, not 63→0.
- Seed an unknown expectation only from a PEC-valid packet.
- A valid but unexpected counter invalidates that read's integrity, records current/sticky evidence, and follows the oracle's observed-value resynchronization behavior. Do not change expectation first and then declare the same packet valid.
- Unexpected zero after a known nonzero expectation retains its own reset evidence.
- Preserve the actual command-effect classification from `cmd_increments_counter`, `cmd_resets_counter` and `wr48_increments_counter`; do not infer it from whether a helper was named “read.”
- A hardware timeout can leave delivery to zero/some/all remote devices unknown. Recovered MCU SPI does not mean remote command completion is known. Mark predictions unknown for affected recipients and require new valid evidence; never report the failed operation as successful or silently replay a mutating command.

The oracle already has explicit resynchronization helpers, including use after uncertain APM operations. Applying a consistent unknown-outcome rule at the new shared seam is a documented hardening decision; differential tests must distinguish this from unchanged successful command behavior.

### 7.3 Separate result layers

`adbms6830_rd48_checked()` can return HAL_OK even when a packet fails PEC or has a counter mismatch; it records those in masks. A port that checks only the returned status will wrongly accept corrupt data.

Each result needs transport status, protocol status, expected/pass/fail masks, observed counters, request generation, direction, timestamps, and validity. The restricted SID probe additionally checks the monitor device-ID field against the reference `0x03`.

Distinguish queue busy, interrupted cooperative wait, coherent-session expiry and ownership violation in the neutral status model. Several old paths encode different conditions as HAL_BUSY/HAL_ERROR; their context must survive the port so a caller cannot mistake “discard this epoch” for “try the next register.”

| Event | Required result and next step |
| --- | --- |
| Bad request, unsupported opcode/direction, queue full | Reject before CS; no hardware reset |
| SPI timeout/I/O failure with successful backend recovery | Current operation failed; payload invalid; session invalidated; later explicit request permitted |
| Backend recovery or CS-idle guarantee fails | Terminal link unavailability; no ordinary retries into the raw API; preserve diagnostic evidence |
| PEC error | Transport may have succeeded; failed packets invalid; no automatic MCU reset merely because PEC failed |
| Counter mismatch/unexpected reset | Integrity failed for this read; retain masks/counters; preserve resync semantics |
| Valid PEC but wrong SID device type | Identity/topology failure; do not call it a working SMB chain |
| Guard expiry inside coherent epoch | Abort epoch; higher layer later owns restart; no mixed publication |
| Wrong-thread/ISR/reentry/corrupt ownership | Software-integrity violation, durable evidence, existing fail-low/fatal policy as applicable |
| Client loses interest or times out | Owner retains active operation and result lifetime until cleanup is complete |

Permanent peripheral unavailability and dead owner software are different. A healthy owner reporting a terminal peripheral fault must not falsely refresh measurement validity, but physical/process fault status alone must not trigger a watchdog reset loop. An owner that stops making software progress remains a liveness failure when that coverage is enabled.

## 8. First target behavior and capability matrix

Use a **restricted probe profile**, proposed filename `app/z016_isospi_probe.conf`. Keep base startup electrically quiet. The probe entry runs from the owner after platform initialization and emits a finite sequence, not an unlimited periodic retry loop.

1. Validate frozen topology and idle-CS state.
2. Bind owner and initialize software link state without sending SRST.
3. Normal wake, RDSID read and full expected-subset PEC/counter/device-ID validation.
4. Read RDCFGA through the same infrastructure and retain raw bytes plus integrity masks. This is observed configuration, not proof that configuration equals desired safety settings.
5. Exercise a bounded two-read awake session and record timing/guard behavior.
6. Exercise checked cold wake as a separate probe case, then repeat read evidence.
7. Publish copied diagnostic results and finish with both CS lines high and SPI idle.

Initial counter state for this read-only probe is **unknown**, because it deliberately does not execute the oracle's SRST initialization. Valid replies establish an observed baseline. Full reset/configuration parity belongs to Z-017.

Only RDSID `00 2C` and RDCFGA `00 02` are transmitted as monitor commands in Z-016's probe. No raw-write escape hatch, WRCFGA/B, ADCV, SNAP/UNSNAP, MUTE/UNMUTE, PWM, COMM, flag-clear or APM command is exposed by the probe. Pure host packing/state tests may cover those encodings without executing them on hardware.

| Capability | Base Z-016 | Probe Z-016 |
| --- | --- | --- |
| SPI adapter and link implementation present | true | true |
| Owner request infrastructure present | true | true |
| Real protocol actor running | false while electrical operations are disabled | true |
| Cell acquisition actor/product | false | false |
| ADBMS safety evidence / temperature evidence | false | false |
| Physical link validation | false until separately evidenced | false until separately evidenced |
| BMS_OK / balancing authority | impossible | impossible |

Use existing `AMS_CAP_ADBMS_ACTOR_LIVE` truthfully for a live protocol actor and add a separate acquisition capability if needed. Do not let “actor live” mean “measurement safe.” Preserve existing watchdog evidence gating; a probe completion is not an ADBMS measurement or temperature heartbeat. Owner runtime progress can be instrumented separately, including failed completed requests.

Urgent balance mute remains a future typed owner action, not a raw-SPI call from the supervisor. Establish interruption/request semantics now; the physical MUTE and durable-zero transaction arrives with Z-020. Do not claim balance-off verification merely because this image cannot issue balancing commands.

## 9. Z-015 contract changes required by Z-016

Update enforcement at the same time as the first real caller, not after a failing target build.

| Existing artifact | Required change |
| --- | --- |
| `check_z015_source_hygiene.py` | Replace stage-specific zero-caller assertions with exact approved executor call sites under the Z-016 profile; retain private-header, owner, no-stock-SPI, no-DMA/async/RTIO and hardware-boundary checks |
| `check_adbms_spi_contract.py` | Base retains zero runtime transfer proof. Probe checks the expected restricted read/wake reachability; exact stock-object exclusion and forbidden final-ELF symbol checks remain |
| `check_z015_elf_caller_gate_selftest.py` | Preserve historical zero-caller fixture and add positive approved-probe plus negative unauthorized-call fixtures |
| `check_capability_contract.py`, `check_runtime_contract.py` | Recognize live protocol owner independently of acquisition/safety evidence; preserve other actors and watchdog policy |
| `check_architecture_contract.py` | Admit only specifically reviewed executor/time files; keep application and pure core free of hardware dependencies |
| `build_manifest.py` | Remove unconditional `runtime_transfer_callers: 0` / raw-entrypoints-absent assertions from the probe profile; report profile, allowed operations, topology, timing implementation and actual capability state |
| Board binding/DTS | Reuse existing SPI and CS metadata. Put fixed electrical wake values in the typed link contract; keep logical topology as application/profile data rather than an invented hardware discovery result |
| CMake composition | Add pure helpers to the existing `ams_core` target and platform files through the existing Zephyr module library. Do not recreate the previous app-side `add_subdirectory(drivers/ams)` error |
| Canonical runner/CI | Add named link/owner/timing/negative gates and target probe profiles while retaining all earlier applicable stages |

The known `spi_stm32.c.obj` substring collision must eventually be corrected to distinguish `adbms_spi_stm32.c.obj` and actual linked stock contributions. Removing the stock-driver prohibition or adding a global “skip SPI contract” option is not acceptable. Work is deferred now; both full target suites must pass before target closeout.

Final ELF checking alone cannot establish runtime thread identity or a complete opcode allowlist. Combine exact build/source checks, runtime owner enforcement, negative tests and observed command traces. In the probe, `write_read` may be linked while the raw write-only entrypoint remains unused; do not require every transport function to appear merely to make a generic checker happy.

## 10. Implementation sequence and review checkpoints

Each step ends in reviewable code/evidence. These are internal increments within Z-016, not a renumbering of the migration roadmap.

| Step | Work | Exit evidence |
| --- | --- | --- |
| 16.1 — Freeze extraction | Record source hashes, active profile, symbol inventory, command-effect table, successful traces and intentional error-reporting changes; audit pinned time APIs | Every selected reference function mapped; no unverified time API assumed |
| 16.2 — Pure protocol | Extract PEC, 4-byte command/48-bit frame helpers, packet masks, counter and session decisions | Differential vectors and independent known-answer tests; null-platform build clean |
| 16.3 — Time and wake | Add checked time seam and private CS pulse operation using existing hardware ownership | Actual production wake code under fake HAL; failure cleanup, wrap and stopped-clock cases pass |
| 16.4 — Single owner | Replace ADBMS placeholder body, add static slots and typed operations, owner identity and result lifetime | Concurrency tests prove no interleaving, stale completions or abandoned buffer references |
| 16.5 — Restricted probe | Bind RDSID/RDCFGA executor, separate valid transport/PEC/counter/SID results, enable explicit profile | Golden end-to-end traces, five-SMB and isolated-one-SMB fixtures; command allowlist proof |
| 16.6 — Build/contracts | Update capability, source, ELF, manifest and runtime gates; resolve deferred checker before target claim | Base + IWDG + probe + probe/IWDG build/contracts; no skipped acceptance gates |
| 16.7 — Closeout | Full applicable host/SIL/analysis campaign, target evidence and separately tracked physical evidence, package/provenance cleanup | Accurate software/target/hardware status and explicit Z-017 handoff |

Do not begin by wrapping all HAL functions or enabling generic `CONFIG_SPI`. The narrow private backend is already the audited transport foundation.

## 11. Validation plan

### 11.1 Differential oracle and independent tests

Build a host harness around the selected **unchanged reference functions**, using fake GPIO/SPI/time only at the old platform boundary. Record CS transitions, delays, TX bytes, RX bytes, return status and integrity/session counters. Compare with the port using identical scripted inputs.

For static functions in the large reference file, use a test-only translation-unit harness or verified extraction with source hashes. Do not reimplement an “oracle” in the test using the same new helpers. Strip neither real error paths nor command-counter handling just to simplify linking. Keep direct known-answer PEC vectors independent of the differential harness.

Successful nominal behavior must match exactly for bytes/order/counters; timing comparisons distinguish requested delay from scheduler-dependent elapsed time. Intentional checked-return, explicit-time-validity and unknown-delivery changes get named expected differences and dedicated tests rather than being presented as byte-identical failure behavior.

### 11.2 Required case matrix

| Gate | Minimum cases |
| --- | --- |
| PEC/framing | Known command vectors; all legal command-option families selected for extraction; read/write PEC distinction; each data/counter/PEC bit corrupted; truncated buffer; zero length; exact capacity and capacity+1; six-byte writes do not read a seventh byte |
| Topology/order | 1/5/6 physical variants with appropriate logical subsets; reusable bound 16; invalid 0/17; capacity smaller than logical count; physical below logical; distinct per-IC payloads proving reverse write order; A/B wire-order metadata; no inferred shorter chain |
| Counter | Unknown seeding, zero reset baseline, 62→63→1, debug off, one-IC mismatch, unexpected zero, invalid PEC cannot seed, timeout with partial delivery, sticky history preserved |
| Wake | 1/5/6 pulse counts and reusable maximum; normal/cold exact requested delays; selected CS only; both never low; no SCK clocks; error at every low/high transition and delay/settle phase; repeated success after recoverable failure |
| Time | Zero epoch; µs and raw-cycle wrap; long idle beyond one raw-cycle wrap; halted/nonadvancing counter; wrong rate; very short delay; interrupted cooperative wait; actual oversleep feeds guard |
| Session | Inactive/owned/nested read session; 2,999/3,000/3,001 µs; non-coherent re-wake; coherent expiry cannot continue; invalid time; direction switch; backend reset; preemption after guard before transfer |
| Ownership/RPC | Sequential wrong-thread calls, ISR call, overlapping call, forbidden rebind, queue full, queued expiry, timeout while ACTIVE, detach/completion race, generation reuse, completed-result expiry, immutable copied arguments |
| Probe validity | All-good five-SMB packets; all-0/all-FF response; missing/truncated packet; wrong SID with valid PEC; only one bad IC; SPI success with bad PEC; RDCFGA observed data does not imply configuration verified |
| Fault recovery | Transfer timeout returns failure despite reset success; next explicit request can retry; reset failure terminal; no recursive recovery loop; original operation error retained alongside cleanup error; no stale result promoted |
| Runtime/authority | Safety/current preempt owner; long waits yield; no queue-drain backlog burst; no placeholder/diagnostic heartbeat promotion; no measurement publication; no BMS_OK/balancing authority |

### 11.3 Negative controls

Deliberate mutations must be rejected for: wrong CS_B; changed wake delays/count source; enabling stock SPI/IRQ/DMA/async; bypassing owner identity; direct service transfer; raw request opcode; caller buffer pointer retained after timeout; guard comparison changed at equality; hidden re-wake during coherent SNAP; wrong PEC counter-bit handling; 63→0; integrity tied to debug flag; bad PEC accepted on transport success; wrong write ordering; stale payload validity; automatic transfer replay; terminal peripheral state counted as successful data; new actor/safety/physical authority inferred from code presence; false zero-caller manifest.

Retain the prior ADC/NVIC/fan/architecture negative tests. Host fakes must expose real pinned APIs, not invented compatibility symbols. Build generated-artifact fixtures for ordinary Windows paths, omitted disabled Kconfig children, private/stock filename collisions and discarded-versus-retained link-map entries.

### 11.4 Target and physical evidence

Build four combinations after implementation: base, IWDG validation, isoSPI probe, and probe+IWDG. Run the complete contract command against every resulting image. Add a separate isolated-one-SMB topology build/fixture; the five-SMB profile remains primary. Proposed probe configuration files do not exist yet, so no command referencing them is presented as runnable now.

Host/SIL cannot prove electrical wake widths, actual chain ordering, target preemption gaps or isoSPI signal integrity. Physical closeout should record PE2/PE4/SCK/MOSI/MISO and received packet evidence for:

- Normal/cold pulse sequence, idle polarities, no simultaneous CS and exact read clock count.
- Five expected SID packets with per-IC PEC/counter evidence, followed by configuration-read evidence.
- Gap distribution around the 3 ms guard under current/safety/fan/IMD load.
- Disconnected or interrupted link and subsequent explicit retry, including CS cleanup.
- Owner stack high-water and worst completed-request duration; safety/current and watchdog behavior under a blocked transfer.

This validates the new Zephyr transport behavior. It does not revoke or demand repetition of the already accepted FreeRTOS five-SMB temperature validation. Full temperature migration evidence is scoped later to the changed Zephyr path.

## 12. How the rest of the ADBMS port follows

| Stage | Port from the oracle | Boundary that must survive |
| --- | --- | --- |
| Z-017 — 6830 core acquisition | `adBms6830_init`, reset/config pack/write/readback, SID-first checks, relevant startup status/POST prerequisites, conversion start, raw cell decoding and `read_voltage_epoch` / `read_cell_voltage_products` | Identity before configuration; configured device count never shrinks; coherent SNAP/UNSNAP cleanup; C remains the authoritative product; signed codes, monitored 15-cell mapping and validity preserved; no full application publication yet |
| Z-018 — AUX/temp/redundancy | GPIO-I²C/COMM mux transactions, ACK decoding, mux-selection validity, AUX/AUX2, thermistor conversion/filtering, C/S and open-wire paths according to the selected profile | 3 muxes/24 sensors per SMB; per-sensor freshness/masks; no stale mux assumption; no automatic S fallback or unapproved C-only authority; missed diagnostic periods skipped rather than replayed |
| Z-019 — protocol recovery | `recovery_check`, configuration fingerprints/readback, session/counter/config requalification and task lifecycle | MCU reset recovery already exists in Z-015; this stage restores remote protocol/configuration confidence. Successful SPI alone cannot restore safety readiness; ambiguous commands and old epochs remain invalid |
| Z-020 — balancing | Accumulator planner, guarded CFGB writes, PWM readback, MUTE/UNMUTE verification, durable zero, task recovery timing and urgent mute | Preserve thresholds/duty/cell limits, minimum-on/recovery behavior and inhibit policy; urgent request never waits on a bus mutex in the supervisor; zero plan is not UNMUTE success |
| Z-021 — APM/2950 | Mixed-chain initialization, advisory primary/redundant measurements, calibration, full-ring-awake token and shared counter coordination | One owner for both device families; SMBs A/APM B; consume awake token before use; successful APM UNSNAP+SNAP+UNSNAP advances SMB expectations by the reference count; uncertain APM outcome resynchronizes expectations; no APM safety authority inferred |
| Z-022 — measurement integration | Selected acquisition/fault/publication portions of `adbms_task.c` and `accumulator.c` | Acquire current-window mutex before capturing voltage boundary; preserve coherent store pinning/sequences, range/uncertainty/calibration provenance, invalid/stale behavior and heartbeat timing; diagnostic results cannot publish as measurements |

The normal voltage-start function has profile-dependent behavior: the reviewed Rev5 path requests redundant continuous conversion, while a separately validated post-ECO profile preserves continuous C and schedules S separately. Port the resolved oracle profile; do not silently substitute whichever branch seems preferable.

Some startup POST/diagnostic dependencies cross the Z-017/Z-018 labels. Extract the needed safety prerequisites together or keep `smb_ready` false until they arrive. Never use a stage boundary as a reason to fabricate passing diagnostic readiness.

### Conversion polling is a distinct future transport operation

`adbms6830_poll_conversion_checked()` sends the poll command and clocks successive single bytes **under one uninterrupted CS assertion**. It waits through propagation (`2 × physical_chain_count` bits), requires observed busy before accepting ready, and has timeout/spin limits. Its callers include C-ADC capture and conversion-timing diagnostics; it is not the ordinary continuous-voltage acquisition loop.

The present Z-015 write/write-read wrappers deassert CS on each call. Repeating them cannot preserve that poll trace. Before those diagnostics are ported, add a dedicated bounded command-and-poll transaction inside the existing backend, with no public begin/end-CS API. Audit the oracle's outer conversion deadline versus nested 500 ms HAL calls and explicitly specify any tightened bound. Do not claim existing wrappers already solve it, and do not make this optional diagnostic extension block Z-016's fixed-length read probe.

## 13. Source review map for implementation

Paths beginning `Core/` are relative to the reference archive's `AMS/` directory; other paths are relative to the current Zephyr repository. Function names, rather than stale line ranges, identify the reviewed source seams.

| Source | Symbols or content reviewed | Use |
| --- | --- | --- |
| `Core/Src/ext_drivers/adbms_shared.c` | `Pec15_Calc`, `pec10_calc`, modular PEC helpers, weak lock hooks | Preserve CRC behavior; exclude weak no-op locking from production port |
| `Core/Inc/ext_drivers/adbms_shared.h` | Frame lengths, 512-byte limit, 1 ms wake intervals | Frozen constants; extract only required neutral types |
| `Core/Src/ext_drivers/adbms6830.c` | `topology_valid`, `wakeup_checked`, `wakeup_cold`, session helpers, counter helpers, `cmd_checked`, `wr48_checked`, `rd48_checked`, SPI wrappers, SID parsing/read | Z-016 extraction and differential traces |
| Same 6830 file | `wait_cooperative`, `us_delay`, `poll_conversion_checked`, initialization, conversion-start and voltage-epoch functions; later function/caller inventory | Timing checks and later-stage dependencies |
| `Core/Inc/ext_drivers/adbms6830_data.h` | Bounds, device ID, driver/session/health/storage structures | Avoid importing the large HAL-coupled context wholesale |
| `Core/Inc/ams_build_profile.h` | Bench/single/five-SMB choices, awake guard and cooperative wait settings | Resolve actual profile instead of relying on comments |
| `Core/Inc/ext_drivers/accumulator.h`, `Core/Src/ext_drivers/accumulator.c` | Topology macros/validation, startup readiness, APM awake token and cross-device counters | Preserve logical versus physical counts and later mixed-ring effects |
| `Core/Src/tasks/adbms_task.c` | DWT wrap extension, cooperative wait/urgent mute, task ordering, voltage-boundary publication, acquisition/balance/APM calls | Owner/timing/integration contracts |
| `Core/Src/ext_drivers/adbms2950.c` and header | Counter-resync and shared-operation entrypoints/caller inventory | Future coupling review; not a complete fresh line-by-line APM audit |
| `docs/DER26_AMS_Codebase_Review_and_Zephyr_Migration_Plan_2026-09-06-v0.5.30.md` | ADBMS section, runtime ownership, Phase 4 and module disposition | Reconcile detailed old scope with current transport design |
| `drivers/ams/adbms_spi_engine.*`, `adbms_spi_stm32.c`, `adbms_spi_internal.h` | Existing APIs, result states, active-operation exclusion, reset recovery and hardware ownership | Reuse, narrow wake extension, runtime owner admission |
| `app/src/ams_threads.c`, `app/Kconfig`, `app/src/main.c` | Placeholder/capability state, priorities, stacks, watchdog evidence and startup-only SPI lifecycle | Correct integration point without phantom safety evidence |
| Current Z-015 source/target checkers, caller inventory, manifest and closeout notes | Zero-caller assumptions, hardware boundaries and actual reported evidence | Stage-aware contracts and truthful closeout |

## 14. Acceptance and next action

Z-016 software acceptance requires the pure protocol/session tests, production wake/timing/owner tests, restricted probe traces, negative controls, retained prior gates, and accurate capability/manifest output. Target acceptance additionally requires the real profile builds and complete contract runs. Physical acceptance remains its own evidence line.

The first implementation action is **16.1: freeze the selected function/profile trace set and audit the pinned time source**, followed by pure protocol extraction. The end of Z-016 should leave a tested, single-owner, read-only isoSPI probe and an explicit handoff to Z-017—not a partially enabled full BMS driver.
