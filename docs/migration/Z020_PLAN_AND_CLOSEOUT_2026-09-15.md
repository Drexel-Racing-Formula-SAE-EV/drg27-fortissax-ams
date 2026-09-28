# Z020 current-board disabled-balancing closeout — 2026-09-15

## Scope and authority

The controlling `docs/z016/implementation_plan.md` defines Z020 as balancing
remaining disabled on the current board. The broader active-balancing scope in
`original_plan_superseded.md` is historical. This implementation completes the
current-board disabled profile and adds a diagnostic shadow planner. It does
not implement or qualify active balancing.

Temperature hardware and pull-ups were previously validated, per the owner.
Temperature scanning remains enabled through Z018. Z019 recovery and per-release
SID, exact CFGA/CFGB, MUTE_ST, and zero PWMA/PWMB audits remain enabled.
BMS authority, balance authority and temperature/ADBMS safety evidence remain off.

## Implementation

`AMS_Z020_BALANCE_DISABLED_VALIDATION` depends on Z019 and disabled authorities.
`app/z020_balance_disabled_validation.conf` enables Z018, Z019 and Z020.
The diagnostic planner has no transport dependency or write interface. The owner
publishes a copied `balance_shadow` value alongside the existing verified MUTE
and durable-zero diagnostics. Initialization, pending/terminal recovery, unknown
continuity, unverified configuration, or unresolved snapshot/config cleanup
preclude a valid shadow plan. Other profiles publish an empty shadow value.

The planner uses the exact FreeRTOS selection policy: minimum among cells at or
above 4100 mV, strictly greater than 20 mV above that cohort minimum, at most four
cells, ascending physical cell index. Cells below 4100 mV do not lower the cohort
minimum. This is not a highest-voltage sort. A valid empty mask means only that
no diagnostic candidate was selected; it conveys no unmute or charge permission.

Inputs must include all 15 usable raw-C cells. Each raw validity bit, 500–5000 mV
range, 2500 ms age and two-miss limit is checked again. Unsigned elapsed time
handles wrap; future timestamps fail the bounded-age check. The extra defensive
checks strengthen the port against an inconsistent copied cell image. AVG8 and
IIR do not influence the planner. `evaluated_ms` timestamps the owner release;
consumers must not treat a copied result as a continuously refreshed decision.

No nonzero DCC/PWM write, UNMUTE, active duty/minimum-on controller, urgent service
queue, or balancing safety authority is added. Existing startup MUTE plus
zeroing/readback and Z019 bounded recovery remain responsible for the inhibited
hardware state. Active balancing qualification is a future separately scoped task.

## Validation and evidence

The production shadow function is compared against an extracted, unchanged
v2.6.27 `accumulator_plan_balance` body using a one-SMB host topology shim:
100,000 deterministic randomized inputs. Directed tests cover threshold equality,
cell cap/index order, missing/raw-invalid data, range, stale/miss boundaries,
wrap, null inputs and clearing a previously populated output.
The actual Zephyr adapter is compiled with Z020 and checks copied shadow
publication, owner restrictions, no additional transport writes and invalidation
on a recovery interruption. ASan/UBSan and GCC analyzer cover the new planner.
24 deliberate unsafe profile/planner/readiness mutations must be rejected.

Run the complete available campaign:

```sh
python scripts/run_z020_host_validation.py .
```

This reruns the Z019 composite and inherited Z018 campaign on this worktree.
Machine-readable results and logs are under `docs/migration/evidence`.
Missing Clang evidence remains explicit; `--require-clang` rejects incomplete
host closeout. Earlier Z015 evidence is historical and is not relabeled as a
fresh full inherited run. No target or physical validation is claimed.

## Target and migration-parity gate

```powershell
west build -p always -b der26_ams app -d build/z020_disabled -- "-DEXTRA_CONF_FILE=z020_balance_disabled_validation.conf"
py scripts/check_all_contracts.py . build/z020_disabled
```

Also rebuild base, Z016, Z017, Z018 and Z019 profiles to exercise target exclusion.
On the known-good single SMB/String B hardware, compare the 24 temperatures and
raw cell data against the validated baseline, verify MUTE/zero readback, compare
shadow masks at threshold/cap boundaries, and exercise reset/disconnect recovery.
The shadow mask must never cause discharge activity. Measure owner timing with
continuous audits and temperature scanning. These are Zephyr migration checks,
not a renewed restriction on the already validated temperature pull-up hardware.

## Oracle provenance

FreeRTOS source path: `AMS/Core/Src/ext_drivers/accumulator.c` in the frozen
v2.6.27 package. Full source SHA-256: 9200b68aad5b78b9871bb591f59485787b13d9d71daa77dbba6e201d653e7510.
The host fixture contains its exact planner body; topology helpers are explicit
host shims. The Z020 ZIP includes a fresh per-file SHA-256 manifest, and packaging
verifies an independently extracted copy against that manifest.
