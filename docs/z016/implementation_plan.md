# DRG27 AMS — Z-016 implementation and revised migration plan

Updated 2026-09-08. Frozen behavioral oracle: DER26 AMS v2.6.27 / firmware 0.5.30. Zephyr source pin: v4.4.0.

## Current outcome

Implemented the restricted one-SMB String B isoSPI link and finite read-only probe. Host evidence is available; STM32 target builds and physical validation of this changed snapshot remain pending. This is a source candidate, not a target/hardware closeout or full ADBMS6830 acquisition port.

The earlier plan incorrectly made five SMBs on String A the primary profile. This revision supersedes that decision. The original is retained in `docs/z016/original_plan_superseded.md` as historical planning evidence only.

## Hardware and oracle profile

The reference `.cproject` has explicit one-SMB and five-SMB bench-validation configurations. The selected baseline is **BENCH Validation 1-SMB**, resolving `AMS_BUILD_PROFILE=5` and `AMS_BENCH_VALIDATION_SINGLE_SMB=1`. Reading a header's fallback alone would select the wrong baseline.

| Property | Selected baseline / implementation |
|---|---|
| Physical route | Temporary ADBMS6822 evaluation jumper; String B, PE4 CS |
| Chain | One physical ADBMS6830 SMB; one logical SMB |
| String A / PE2 | Held inactive; probe cannot select it; no fallback |
| SPI | Existing private SPI6, mode 3, 421875 Hz, /256 from 108 MHz |
| GPIO/timing | Existing CS owner; 1000 us low then 1000 us high per wake train |
| Cold wake | Two trains, 5 ms cooperative settle, fresh normal wake before the subsequent read |
| Voltage policy | FreeRTOS C_ONLY_MVP degraded mode; C/S comparison is not qualified |
| S diagnostics | Periodic S diagnostic disabled; S-path ECO validation false |
| Balancing / discharge timer | Disabled; no balance commands or authority path in Z016 |
| AUX2 / thermistor open-wire | Disabled in the selected bench profile; not ported here |
| Automatic temperature mux scan | Disabled; no automatic scans introduced |
| Intrusive C open-wire / fault injection | Disabled; no commands introduced |
| APM2950 / standalone APM / HV dividers | Disabled; no APM traffic or mixed ring |
| BMS_OK authority | Remains absent; existing fail-low behavior retained |
| IWDG | Base remains disabled; existing explicit validation configuration retained |

C_ONLY_MVP is a degraded voltage-validity policy, not proof that every legacy S conversion bit is zero. The legacy continuous conversion setup can still make S observations depending on hardware revision. Z016 starts **no conversions at all**. When Z017 ports C acquisition it must follow the actual selected oracle branch, without enabling C/S authority or S diagnostics on this board. AVG8/filter settings and startup POST in the oracle are future acquisition work, not silently reclassified as disabled features.

The attached 6821/6822 reference describes CS wake signaling rather than a separate register-programmed transceiver driver. The attached 6830B reference supplies PEC/counter and wake/idle behavior. These references do not establish that the present board's S channel or balancing hardware is qualified.

## Implementation

`lib/ams_core/adbms/ams_adbms_link.c` contains the portable PEC15/PEC10 implementation, typed SID/CFGA command construction, six-byte response validation, command-counter trust and awake-session guard. Both commands are read-only and do not advance the device counter. An initially unknown counter is seeded only by PEC-valid data; a mismatch invalidates that read and adopts the observed counter for the next read. Counter zero is accepted only as an initial/reset observation or reported as a mismatch after another value. PEC, transport and clock failures discard session/counter trust. Every unsuccessful read leaves a zeroed, invalid output.

A 3000 us guard uses an explicit validity flag, so timestamp zero is not an invalid sentinel. Guarded operations reject an expired session; they cannot silently re-wake and splice a coherent operation across wake epochs. Standalone operations can re-wake. Cold settling explicitly leaves the awake session invalid because 5 ms can exceed the minimum isoSPI idle timeout.

`drivers/ams/adbms_time_zephyr.c` uses pinned Zephyr's real `CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER` and `TIMER_HAS_64BIT_CYCLE_COUNTER` capabilities. The inspected SysTick cycle read takes a short internal spinlock and performs finite register reads; no peripheral polling loop was found. Short delays use cycle deadlines plus the oracle-style finite iteration fallback of 1024 + 256 * microseconds. Each call is limited to 1000 us. PM is excluded. A clock stall fails instead of hanging. Hardware timing and preemption still need measurement.

`adbms_spi_stm32.c` retains sole GPIO/SPI hardware ownership. The new private wake operation holds the existing ACTIVE state across its pulse train, leaves SPI disabled, and deasserts both CS outputs on every exit. GPIO/timing failure faults the link. The probe binds one thread identity; raw transfer calls in the probe image reject any other thread, ISR context or String A selection. The existing bounded SPI transfer/recovery behavior is retained.

`drivers/ams/adbms_link_probe.c` executes at most one finite operation per existing 100 ms ADBMS release:

1. Normal wake.
2. Standalone SID read and 6830 identity validation.
3. Standalone CFGA read followed immediately by guarded SID, with no logging between them.
4. Cold wake, settle, fresh normal wake and SID read.

The probe stops after the first error or after the fourth successful step. Logging occurs with CS inactive. It does not publish measurements, fabricate an ADBMS watchdog heartbeat, write configuration, reset monitor counters, start conversions, issue SNAP/MUTE/PWM/COMM commands or access APM.

Base `prj.conf` keeps the electrical probe disabled. `z016_isospi_probe.conf` explicitly enables it, using the same fixed String B/one-SMB topology. No onboard profile is enabled pending the new board.

## Revised scope of owner dispatch

The original plan proposed four asynchronous request/result slots with generation IDs, cancellation and client expiry. This implementation has **no external request API**: the finite probe runs directly in its sole owner thread and retains no caller pointers. Therefore queue lifetime, caller cancellation and multi-client arbitration are deferred until a later stage actually introduces CLI/service requests. They are not claimed implemented or tested. At that point implement the originally specified copied arguments, generation-safe slots, detached-active lifetime and urgent-operation policy before exposing such requests.

Likewise arbitrary packet writes, variable chain topology, multi-device ordering, counter-increment classification for mutating commands and mixed APM sessions are deferred. Only the two supported read commands can be constructed by this target path. This smaller interface implements the present link bring-up without enabling the legacy driver's unrelated capabilities.

## Existing work preserved

Z001–Z014 board, portable algorithms, current ADC, fans, IMD and watchdog behavior remain the inherited baseline. Z015 retains private synchronous SPI6 ownership, reset recovery, IRQ pending-clear, no generic SPI device, no DMA/async/RTIO and startup without wire activity. ADC1/2 common-reset ownership and ADC3 exclusion remain intact. Output-only fan NVIC quiescence and TIM2 IMD capture are retained.

The previously reported linker-map bug is fixed using an object-name boundary: `adbms_spi_stm32.c.obj` no longer falsely matches stock `spi_stm32.c.obj`. The stock driver remains forbidden. Base ELF checks still require zero raw transfer entrypoints. Probe ELF checks require the read/wake path and continue to reject the write entrypoint. Build metadata now distinguishes base from the explicit probe profile. Existing acquisition/safety capability flags stay false; probe enablement is reported separately.

## Validation evidence and limits

The Z016 unit suite compiles the actual new core, timing backend, wake adapter and finite probe. An unchanged copy of the reference `adbms_shared.c` and header supplies the differential oracle.

- 100000 randomized payloads: 200000 PEC15/PEC10 comparisons per execution.
- All 64 counter values, unexpected zero, PEC corruption, identity rejection, invalid operation, clock/IO failures, 2999/3000 us session boundaries and backward time.
- Cold settle invalidates its session and requires a fresh wake.
- Production wake: thread/ISR and String A rejection, both CS high on exit, no clocks during wake, two-train timing, stalled clock and GPIO assertion failure.
- Production probe: permitted frames only, four-step completion, bad-PEC stop, no repeat over 20 owner releases.
- Eleven source mutations reject topology, command, PEC, counter, session, owner, timer and authority weakening.
- Inherited host gate: 44 available recorded stages passed, including unit/SIL suites, ASan/UBSan, GCC analysis, 18 Z014 mutations and 47 Z015 mutations.

Clang analysis was unavailable. ThreadSanitizer was not requested/performed in this continuation. LeakSanitizer's process inspection is incompatible with this sandbox; `LSAN_OPTIONS=detect_leaks=0` was used while ASan/UBSan remained active. The new link uses no heap. The inherited 54-stage historical closeout is not relabeled as a new 54-stage run.

No STM32 target compilation, target timing capture, flashing or physical testing was performed on this new snapshot. Earlier Z015 target successes do not validate the new code. Do not claim full Z016 hardware closeout, C-cell acquisition, S redundancy or balancing on this evidence.

Run the new host entrypoint with `python scripts/run_z016_host_validation.py .`; inspect the inherited JSON report for environment skips. The full target checker now also invokes `check_z016_link_contract.py`.

## Target validation commands

From PowerShell after applying this source snapshot:

```powershell
Set-Location 'C:\DER_AMS\git\revert\DER27-AMS-zephyr'
west build -p always -b der26_ams app -d build\z016_base
py scripts\check_all_contracts.py . build\z016_base
west build -p always -b der26_ams app -d build\z016_iwdg -- "-DEXTRA_CONF_FILE=z014_watchdog_validation.conf"
py scripts\check_all_contracts.py . build\z016_iwdg
west build -p always -b der26_ams app -d build\z016_probe -- "-DEXTRA_CONF_FILE=z016_isospi_probe.conf"
py scripts\check_all_contracts.py . build\z016_probe
```

This is compile/check authorization, not an assertion of physical readiness. For the subsequent bench run capture PE4 low/high periods and verify PE2 stays high, confirm correct B jumper wiring, SID/CFGA PEC and identity, and that the probe stops on a disconnected/bad chain. Measure scheduler preemption and cold-wake settling. Keep BMS_OK low and balancing absent.

## Remaining migration sequence

| Stage | Next implementation / closeout |
|---|---|
| Z016 | Three fresh target builds/contracts; physical String B link evidence; review timing under preemption |
| Z017 | Exact one-SMB C-only initialization and cell acquisition, startup config/readback/POST, no unsupported S authority |
| Z018 | AUX/temperature paths with disabled defaults preserved; no automatic scan or redundancy enablement without the appropriate profile |
| Z019 | Protocol recovery and interruption policy; counter reset/resync and snapshot invalidation for mutating operations |
| Z020 | Balancing remains disabled on current board; future explicit reviewed hardware/profile qualification |
| Z021 | APM and mixed topology, explicit new-board profile; no automatic String A migration |
| Z022 | Measurement publication, lock-before-voltage-boundary timestamp, current-window/estimator integration, genuine heartbeat promotion |

Before any future service queue, implement copied typed requests, finite expiry and generation-safe result ownership. Before any future multi-device topology, test physical/logical mapping and reverse write ordering against the frozen oracle. Before any authority promotion, require the corresponding acquisition/fault behavior and physical evidence; a successful SID read is link evidence only.
