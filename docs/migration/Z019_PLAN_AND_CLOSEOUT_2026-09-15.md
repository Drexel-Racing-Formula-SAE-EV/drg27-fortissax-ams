# Z019 protocol recovery — plan and implementation record

## Scope and reference

Stage authority: `docs/z016/implementation_plan.md` assigns Z019 protocol
recovery/interruption, counter reset/resynchronization and snapshot invalidation.
The frozen FreeRTOS oracle is v2.6.27, particularly `adbms6830_recovery_check()`
and the configuration fingerprint helpers in `AMS/Core/Src/ext_drivers/adbms6830.c`.
The Z018 rebuild is the implementation baseline. No Z020 balancing work is included.

## Lifecycle policy implemented

1. Z019 is an explicit opt-in extension of the existing Z018 owner profile,
   not another independent SPI owner. Primary temperature scanning remains enabled.
2. Before each normal owner release, audit the exact six-byte startup SID,
   CFGA including MUTE_ST, production CFGB, and zero PWMA/PWMB. Each read uses
   PEC10 and the current command counter. Unknown counter, pending SNAP cleanup,
   unproven configuration or unresolved temporary configuration blocks normal work.
3. Configuration fingerprints use the oracle FNV-1a order (IC index, CFGA, CFGB).
   Both expected and observed fingerprints include the expected read-only MUTE_ST.
   Hashes are diagnostic; exact bytes determine acceptance.
4. An interruption immediately withdraws cell/AVG8/IIR freshness, usability,
   temperature/filter history, mux selections and configuration/live status.
   Raw numbers and timestamps may remain for diagnostics, with validity withdrawn.
   UNSNAP and temporary-configuration obligations are not forgotten.
5. No repair happens in the detecting release. The next owner release attempts
   exactly one bounded repair: prove pending UNSNAP cleanup; recheck SID before
   mutating the device; perform the existing SRST/config/baseline/POST/MUTE/zero
   initialization; require a fresh coherent epoch of all 15 monitored cells;
   finally prove SID/configuration/PWM readback again. Only then commit the candidate.
6. Existing initialization POST attempts and whole-cell-epoch attempts retain
   their inherited limits. There is one outer repair attempt per interruption.
   Recovery performs no loop of successive reinitializations.
7. Successful repair increments a generation and preserves accumulated transfer,
   counter, startup, POST and temperature diagnostic history. Temperature and IIR
   settling history rebuild from new acquisitions. This restores acquisition
   confidence, not safety evidence or vehicle authority.
8. Failed repair is terminal until reboot. Identity mismatch, illegal owner and
   terminal SPI-backend failure also latch directly. Later owner releases issue
   no new repair transfers. The SPI backend is not reopened behind its fault latch.
9. The existing owner skips normal acquisition in a release that performed repair.
   Its existing absolute schedule skips missed deadlines; no catch-up burst is added.
10. Recovered transport/PEC/counter errors inside an otherwise successful normal
    cell retry set a continuity-loss flag. Z019 withdraws that candidate rather
    than allowing a successful return value to hide the interruption.

## Intentional differences from FreeRTOS

FreeRTOS `recovery_check()` continues collecting diagnostics after earlier errors.
Z019 stops at a failed qualification boundary and latches a failed outer repair.
Z019 repeats the existing full startup POST and safe balancing inhibit during
repair, a stronger check than the oracle's configuration-write/readback and C-ADC
recovery diagnostic. It additionally checks the complete original SID, not only
its part-ID bits. Old measurements cannot be reused to establish recovery.

The identity/configuration audit runs every 100-ms owner release. Timing and stack
headroom must be measured on the STM32 target; the host tests do not establish WCET.
Recovery can exceed one normal release period because it includes POST; missed
releases are skipped. There is no ADBMS watchdog safety heartbeat to fabricate.

## Source changes

- Recovery core/state, immutable configuration audit, interruption withdrawal,
  single-attempt repair, candidate commit and preserved history.
- Z019 owner adapter gating and copied recovery diagnostics.
- Explicit `app/z019_recovery_validation.conf` layered over Z018.
- Z019-aware build manifest and source/target contract integration.
- Z019 negative controls, core fault tests and actual adapter control-flow tests.
- Inherited Z018 checker narrowed to its own diagnostic functions so it does not
  mistake the new lifecycle's required history invalidation for diagnostic leakage.

## Verification and limits

Run `python3 scripts/run_z019_host_validation.py .`.
Use `--require-clang` for the strict host completion gate.

`evidence/Z019_HOST_REPORT.json` records the current executed campaign and binds
its source files by SHA-256. The nested Z018 campaign exercises the expanded
production monitor, inherited startup/cell/SNAP behavior, temperature behavior,
source/mutation gates, thermistor oracle tests, ASan, UBSan, GCC analysis and
Z016 protocol/probe tests. Actual adapter builds cover Z017, Z018, Z018 diagnostics
and Z019. Z019 has 25 source mutations; inherited counts remain Z018 54, Z017 26,
Z016 12, plus the Z014/Z015 negative campaigns.

Directed Z019 tests cover configuration drift, fingerprint agreement, reset to
counter zero, discarded history, fresh successful recovery, exact SID mismatch,
UNSNAP failure, terminal no-retry behavior, hidden retry continuity loss, sticky
history preservation and every read/write boundary of the repair sequence.
An internal POST/epoch retry may recover a transfer fault; such success still
must prove fresh monitored cells and exact safe configuration.

Clang remains unavailable. No target build, target ELF check, physical timing,
stack measurement or Zephyr hardware parity test was performed. LeakSanitizer
is not claimed; the inherited execution environment required leak detection off.
The prior Z015 full report is retained as historical evidence, not relabeled as
an entirely new full Z015 run against Z019. Executed focused tests passing do not
mean full host/target/hardware closeout is green.

## Target and physical validation

Build all retained profiles using the Z018 checklist, plus:

```powershell
west build -p always -b der26_ams app -d build/z019_recovery -- "-DEXTRA_CONF_FILE=z019_recovery_validation.conf"
py scripts/check_all_contracts.py . build/z019_recovery
```

On the already-validated temperature hardware, validate Zephyr parity first.
Then reset/disconnect/reconnect the monitor, corrupt configuration/readback and
interrupt an active epoch. Check generation, original error, pending/terminal
state, counter history, all freshness masks and exact MUTE/DCC/PWM readback.
Prove a failed repair produces no repeated SPI traffic and a successful repair
cannot publish old temperatures. Measure normal audit and recovery duration,
release skipping and stack watermark. Use diagnostic-enabled Z018 options only
when explicitly testing that combined profile.

BMS_OK, balancing authority, ADBMS/TEMP safety evidence, APM and estimator
publication remain disabled. No vehicle release is authorized by this stage.
