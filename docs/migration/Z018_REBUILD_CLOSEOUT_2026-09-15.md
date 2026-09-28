# Z018 reconstruction and validation record

Rebuilt 2026-09-14–15 from the retained Z017 full repository and
DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06 FreeRTOS oracle.
The previously described Z018 working directory was lost. This is a new
implementation and new test evidence; old Z018 pass claims were not reused.

## Delivered behavior

- String B / PE4, one ADBMS6830B, existing priority-3 100-ms owner.
- Z017 initialization, POST, startup MUTE/zero/readback and cell/SNAP behavior retained.
- Z018 primary temperature scanning enabled: 0x4C/GPIO1/sensors 0–7,
  0x4D/GPIO2/sensors 8–15, 0x4E/GPIO3/sensors 16–23.
- Three sequential sensor captures per release, same local mux position,
  eight releases per complete scan. One attempt; failed releases advance once.
- WRCOMM, exact pre-STCOMM readback, 13-byte STCOMM, RDCOMM and independent
  address/data ACK validation. Data ACK 0x07 and 0x77 are accepted.
- 3-ms mux settle, 4-ms primary conversion wait, 1-ms successful-capture guard.
- Per-sample capture timestamps; earlier successful samples survive later failures.
- AUX sentinels 0xFFFF/0x8000 do not publish; raw zero is valid.
- Exact imported v2.6.27 thermistor model and 281-point manufacturer LUT,
  10-kohm nominal divider pull-down, 5.0-V reference, existing clamped endpoint semantics.
- History: 12,000-ms age, at most ten misses, 25-C jump and 5-C/s absolute-rate
  diagnostics, sign-aware 1/8 IIR. Diagnostic flags do not invent safety authority.
- Optional AUX2: 9-ms wait, >20-mV disagreement, no primary substitution.
- Optional thermistor OW: CFGA byte 2 = 0xB8, 6-ms down/up waits,
  >=10-mV response, <=20-mV recovery difference, exact production restoration
  including MUTE_ST-aware readback. Uncertain temporary writes still require restoration.
- Failed restoration faults the monitor. A successful recovery does not erase
  the original diagnostic error. Diagnostic samples never enter normal history.
- Optional diagnostics run at 250/2000-ms deadlines without catch-up bursts.
- Adapter rejects ISR/non-owner calls before mutating monitor state.

## Validation status

See `evidence/Z018_REBUILD_HOST_REPORT.json` and `Z018_REBUILD_HOST.log` for
new production test results and source hashes. The focused runner includes
source gates, Z014/Z015/Z016/Z017/Z018 negative controls, the production monitor,
three adapter configurations, original thermistor tests, ASan, UBSan, GCC
analysis of monitor/protocol/history/model, and Z016 link/probe regression.
Z018 rejects 54 source mutations; Z017 rejects 26; Z016 rejects 12.
The monitor tests include 50,000 randomized cell-history and 50,000 randomized
temperature-history steps, transfer-boundary failures, uncertain STCOMM,
uncertain remote CFGA acceptance, restoration failures and partial captures.

`evidence/Z018_INHERITED_Z015_REPORT.json` reports successful executed inherited
stages and successful ThreadSanitizer evidence. Clang was unavailable and is
explicitly listed as skipped. The later acyclic Kconfig correction is covered by
the final source/mutation campaign; that old report is not presented as a fresh
complete target/configuration build after the correction.

Limitations: Clang static analysis remains outstanding. LeakSanitizer could not
run in the traced execution environment; ASan memory checks and UBSan did run.
Inherited runs used `LSAN_OPTIONS=detect_leaks=0`; no leak-detection pass is claimed.
No STM32/Zephyr target build, ELF validation or physical migration test was run.
Therefore this package is a rebuilt implementation with passing executed host
checks, **not full host/target/hardware closeout-green**. The strict runner keeps
missing evidence visible and fails the completion gate while it remains missing.

## Target commands

From the repository in the existing pinned Zephyr workspace:

```powershell
west build -p always -b der26_ams app -d build/z018_base
py scripts/check_all_contracts.py . build/z018_base
west build -p always -b der26_ams app -d build/z018_iwdg -- "-DEXTRA_CONF_FILE=z014_watchdog_validation.conf"
py scripts/check_all_contracts.py . build/z018_iwdg
west build -p always -b der26_ams app -d build/z018_z016 -- "-DEXTRA_CONF_FILE=z016_isospi_probe.conf"
py scripts/check_all_contracts.py . build/z018_z016
west build -p always -b der26_ams app -d build/z018_z017 -- "-DEXTRA_CONF_FILE=z017_cell_validation.conf"
py scripts/check_all_contracts.py . build/z018_z017
west build -p always -b der26_ams app -d build/z018_temp -- "-DEXTRA_CONF_FILE=z018_temp_validation.conf"
py scripts/check_all_contracts.py . build/z018_temp
west build -p always -b der26_ams app -d build/z018_aux2 -- "-DEXTRA_CONF_FILE=z018_aux2_validation.conf"
py scripts/check_all_contracts.py . build/z018_aux2
west build -p always -b der26_ams app -d build/z018_ow -- "-DEXTRA_CONF_FILE=z018_therm_ow_validation.conf"
py scripts/check_all_contracts.py . build/z018_ow
```

## Physical migration parity checklist

Temperature hardware/pull-ups are already validated per the user. There is no
new electrical-validation prerequisite to running the Z018 profile.

1. Confirm startup SID/POST/MUTE/config results and BMS_OK low.
2. Verify all 24 labels against physical sensor routing and known-good FreeRTOS readings.
3. Measure COMM/STCOMM waveforms, ACK behavior, 3/4/1-ms timing and eight-release scan.
4. Disconnect/reconnect a sensor/mux; inspect invalid, fresh, retained and stale masks.
5. Confirm completed earlier samples remain available after a later-mux failure.
6. Validate delayed releases skip catch-up work; measure stack margin and owner timing.
7. Test AUX2 and OW separately using their explicit profiles. Verify diagnostic-only
   results, exact CFGA restoration and fail-closed behavior on restoration failure.
8. Record migration comparison evidence before any later safety-authority promotion.

BMS_OK, balancing, ADBMS/TEMP watchdog safety evidence, APM and estimator publication
remain disabled. This stage does not authorize vehicle release or advance to Z019.
