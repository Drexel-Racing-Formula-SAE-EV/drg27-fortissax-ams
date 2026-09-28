# Z024 reviewed portable CAN checkpoint — runtime integration incomplete

Expanded review: [Z013–Z024 review](docs/migration/Z013_ONWARDS_REVIEW_2026-09-22.md).

Earlier review fixes and limitations: [2026-09-22 review](docs/migration/Z024_REVIEW_2026-09-22.md).

Adds the frozen CAN scheduler, bounded callback/owner transport core and current
provenance codec. CAN hardware remains disabled; the latest live profile is Z023.
[Implementation, tests and remaining work](docs/migration/Z024_IMPLEMENTATION_STATUS.md).
Run `python3 scripts/run_z024_host_validation.py .` for the checkpoint campaign.
Earlier stage notes below are historical.

# Z023: shadow fault supervision and owner-progress validation

Z023 adds a tested safety-thread cycle, coherent copied fault decisions,
owner-bound current/ADBMS/TEMP progress and segment voltage/temperature policy.
Temperature scanning remains enabled. BMS_OK, balancing and new safety-evidence
capabilities remain disabled. Target and hardware qualification are not claimed.
See [Z023 status and evidence](docs/migration/Z023_IMPLEMENTATION_STATUS.md) and
[target/physical checklist](docs/migration/Z023_TARGET_CHECKLIST.md).
Earlier checkpoint notes below are historical.

# Z022 update: segment estimator and current-fault processing

The restored checkpoint now runs advisory segment EKFs and the startup/precharge
current-fault classifier. Missing SMBs stay invalid; temperature scanning stays enabled.
[Current implementation and remaining closeout gates](docs/migration/Z022_COMPLETION_REVIEW_2026-09-15.md).
Full safety/target/hardware closeout remains open. Earlier notes below are historical.

# Current checkpoint: Z022 single-SMB publication

Z022 adds an explicit profile for current sampling, lock-before-boundary current
integration and copied single-SMB measurements. Temperature scanning stays enabled.
Full-pack estimator execution and safety-heartbeat promotion remain incomplete.
See [scope, validation and target commands](docs/migration/Z022_PUBLICATION_STATUS_2026-09-15.md).
Earlier checkpoint notes below are historical.

# Current checkpoint: Z021 portable foundation — incomplete

Portable mixed-ring coordination and primary APM decoding are implemented and
host-tested. The live runtime remains Z020; no APM or String A traffic is enabled.
[Completed work and remaining Z021 requirements](docs/migration/Z021_FOUNDATION_STATUS_2026-09-15.md).

# Current package: Z020 — balancing disabled, diagnostic shadow

Z020 retains enabled temperature scanning and Z019 recovery/zero-state audits.
It adds a copied, diagnostic-only FreeRTOS balance candidate mask; active
balancing remains disabled under the current-board migration plan.
See [Z020 scope, validation and target steps](docs/migration/Z020_PLAN_AND_CLOSEOUT_2026-09-15.md).
Host evidence limitations remain recorded; target and hardware gates are open.

# DER27 AMS — Z019 protocol recovery

Z019 adds exact remote identity/configuration checks, interruption-driven data
invalidation and one bounded repair attempt in the existing Z018 owner.
Temperature scanning remains enabled. Failed repair latches until reboot.
BMS_OK, balancing and safety-evidence capabilities remain disabled.

Current plan, behavior, tests, limitations and build commands:
`docs/migration/Z019_PLAN_AND_CLOSEOUT_2026-09-15.md`.

Host campaign: `python3 scripts/run_z019_host_validation.py .`
Strict host gate: add `--require-clang`.

## Historical Z018 baseline README

# DER27 AMS — Z018 rebuilt temperature validation

Z018 has been rebuilt from the Z017 repository and frozen FreeRTOS v2.6.27 source.
The explicit Z018 profile enables primary temperature scanning: three ADG728 muxes,
24 sensors, three captures per 100-ms owner release. Optional AUX2 and thermistor
open-wire diagnostics are off in the normal profile.

The temperature hardware and pull-ups were previously validated by the user.
Zephyr implementation parity, target builds and physical timing remain to be checked.
BMS_OK, balancing authority, temperature safety evidence and ADBMS safety evidence
remain disabled. No Z019 implementation is included.

See `docs/migration/Z018_REBUILD_CLOSEOUT_2026-09-15.md` for current evidence,
limitations, build commands and the hardware parity checklist. Historical Z017
reports below are baseline evidence, not validation of the rebuilt Z018 code.

Host validation: `python3 scripts/run_z018_host_validation.py .`
Strict completion gate: add `--require-clang` (fails when required evidence is unavailable).

## Historical Z017 baseline README

> Current snapshot: **Z017 String-B / one-ADBMS6830 initialization + coherent cell-acquisition host/SIL closeout.** See `docs/migration/Z017_IMPLEMENTATION_CLOSEOUT_2026-09-08.md`, `docs/migration/Z017_PACKAGE_CLOSEOUT_2026-09-08.md`, and `docs/migration/Z017_HARDWARE_VALIDATION_CHECKLIST_2026-09-08.md`.

author: @Mahad-Faisal
WORK IN PROGRESS R&D MAIN REPO IS DER26AMS

Current migration stage: **Z017 source/host/SIL GREEN; target and physical validation pending.** Z017 is a no-authority validation profile: BMS_OK assertion, balancing authority, temperature safety evidence, estimator/combined pack publication, APM/COMM and ADBMS watchdog safety evidence remain disabled. Z018+ is not started.

## Z017 selected boundary

- String B / PE4 only;
- one physical ADBMS6830B;
- 16 register channels read, first 15 monitored;
- raw C authoritative; AVG8/IIR advisory only;
- production CFGA `81 00 00 FF 03 03`, CFGB `71 52 46 00 00 00`;
- current-board ADCV `03 E0`;
- existing priority-3 ADBMS thread is the single SPI owner on a 100-ms absolute schedule;
- startup includes checked identity/config/POST and private fail-safe MUTE + zero balancing readback;
- coherent SNAP epochs allow one whole-epoch retry and no second ADCV;
- no String-A fallback, multi-SMB expansion, UNMUTE, or nonzero balancing API.

## Migration architecture invariant

`lib/ams_core` remains host-native portable C. Hardware access belongs behind narrow `drivers/ams` interfaces. The reviewed direct STM32 seams from earlier stages remain narrow: board fail-low, private SPI6, private current ADC, and fan NVIC hardening. Z017 adds protocol/monitor/cell-image logic above the existing private SPI6 seam rather than adding another raw hardware owner.

The application remains static/no-heap. Z016 and Z017 are mutually exclusive profiles. Raw SPI/opcode/string-selection APIs do not escape the owner boundary.

## Canonical Z017 host/SIL gate

```text
python scripts/run_z017_host_validation.py . --require-clang \
  --inherited-report docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_REPORT_2026-09-08.json \
  --report docs/migration/evidence/Z017_HOST_SIL_CANONICAL_REPORT_2026-09-08.json
```

Final result: **13/13 Z017 composite stages passed in 59.403 s**. The consumed inherited Z015 canonical report is independently green at **56/56 stages in 323.540 s**, with no skipped evidence and ThreadSanitizer performed. Z017 rejects **26** unsafe mutations; Z016 rejects **12**. No target build, hardware validation, authority promotion, or Z018+ work is claimed by these reports.

Primary Z017 evidence:

- `docs/migration/Z017_ORACLE_FREEZE_2026-09-08.md`
- `docs/migration/Z017_IMPLEMENTATION_CLOSEOUT_2026-09-08.md`
- `docs/migration/Z017_PACKAGE_CLOSEOUT_2026-09-08.md`
- `docs/migration/Z017_HARDWARE_VALIDATION_CHECKLIST_2026-09-08.md`
- `docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt`
- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_HOST_SIL_CANONICAL_REPORT_2026-09-08.json`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_CANONICAL_2026-09-08.log`
- `docs/migration/evidence/Z017_INHERITED_Z015_HOST_SIL_REPORT_2026-09-08.json`

## Target contracts

After unpacking this exact package in the real Zephyr 4.4 STM32F767 workspace, build and check all four profiles:

```powershell
west build -p always -b der26_ams app -d build\z017_base
py scripts\check_all_contracts.py . build\z017_base

west build -p always -b der26_ams app -d build\z017_iwdg -- "-DEXTRA_CONF_FILE=z014_watchdog_validation.conf"
py scripts\check_all_contracts.py . build\z017_iwdg

west build -p always -b der26_ams app -d build\z017_z016_probe -- "-DEXTRA_CONF_FILE=z016_isospi_probe.conf"
py scripts\check_all_contracts.py . build\z017_z016_probe

west build -p always -b der26_ams app -d build\z017_cell_validation -- "-DEXTRA_CONF_FILE=z017_cell_validation.conf"
py scripts\check_all_contracts.py . build\z017_cell_validation
```

Target-green status requires all four builds and contract runs. Physical Z017 validation remains separate even after those pass.
