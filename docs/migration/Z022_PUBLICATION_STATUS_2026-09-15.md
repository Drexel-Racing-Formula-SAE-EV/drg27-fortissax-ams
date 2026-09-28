# Z022 single-SMB publication integration — 2026-09-15

## Status and supported profile

Implemented: the selected single-SMB current acquisition/publication path and
full-pack estimator admission checks. Full Z022 is NOT complete: five-segment
estimator execution, current/fault qualification and genuine safety-heartbeat
promotion remain open. Z021's new-board mixed-ring integration remains incomplete.
This package makes no target-build, timing or hardware-validation claim.

`app/z022_measurement_validation.conf` explicitly layers Z022 on Z020/Z019/Z018.
The default and earlier profiles retain their prior runtime behavior. Z022 enables
current acquisition in the existing current worker, retains the single String-B
owner, and keeps temperature scanning enabled. Temperature hardware/pull-ups were
already validated by the owner; this work does not impose a renewed hardware gate.
Balancing, BMS authority, and ADBMS/temperature/current safety evidence remain off.
The hidden current-actor capability is true only for the Z022 profile.

## Ordering and publication

The current worker reads the existing bounded HIGH-then-LOW ADC adapter, converts
through the existing oracle-derived sensor model, then takes the shared current
mutex before timestamping and updating the current window. ADC polling does not
hold this mutex. Calibration ID, record confidence, selected range and uncertainty
travel through the immutable window. No zero calibration or confidence is invented;
unloaded calibration remains unconfident with unknown uncertainty.

Immediately after cell acquisition and before the temperature scan, the ADBMS
owner takes the same mutex, reads the voltage-boundary time, rotates the current
window, then releases the mutex. An early failed/recovery release uses its cleanup
return point as the boundary. The mutex has a finite 2 ms acquisition timeout.
A dropped current update atomically taints the next accepted update/boundary;
a failed boundary lock drops publication rather than publishing a stale window.

The owner publishes after the temperature phase using the existing two-buffer
measurement store. Metadata uses a separate spinlock, readers pin only while
copying, and a pinned inactive buffer causes a drop. Each captured boundary is
consumed once, including on store drops; cumulative charge and sequence gaps
remain observable. Readers must validate age when using a retained snapshot.

Slot zero contains the selected SMB. Slots 1–4 remain absent with zero usable
masks and UINT32_MAX ages. Raw C remains authoritative; AVG8/IIR stay advisory,
and unready IIR masks are zero. Per-cell and per-temperature validity, misses,
ranges and individual timestamps are rechecked at publication. Recovery,
configuration uncertainty or unresolved cleanup withdraws SMB validity. Valid
current can remain independently observable while voltage is unavailable.

A one-SMB publication does not set full-pack voltage/temperature validity flags.
Its per-segment masks carry the actual bench data. The estimator admission helper
requires all five segments, fresh voltage/temperature/current, matching current
and voltage boundaries, calibration provenance and known nonzero uncertainty.
The current profile cannot satisfy it. No EKF step is run against fabricated
missing segments; actual full-pack estimator execution remains unconnected.
No successful publication is relabeled as watchdog safety evidence.

## Validation

Run `python scripts/run_z022_host_validation.py .`.

The campaign covers:
- Crossing-current reproduction: lock contention advances current to 110 ms after
  the caller arrived at 100 ms; boundary is 110 ms and the 90–110 ms charge is
  0.2 As at 10 A, without reassigning a crossing sample to an old boundary.
- Lock failures, immutable/drop publication, consume-once boundaries, invalidation,
  stale/wrapped timestamps, missing segments and estimator admission.
- The actual new Zephyr adapter under host stubs: separate current/voltage owners,
  ISR rejection, ADC failure, boundary timing and copied publication.
- The actual monitor adapter compiled with Z022, checking that its boundary hook
  executes before temperature acquisition.
- ASan/UBSan, GCC analyzer, 15 source mutations, current-window regression and
  concurrent pinned-store regression, plus the inherited Z021/Z020/Z019/Z018
  campaigns on this source tree. Logs and reports are in `docs/migration/evidence`.

Clang, CMake and west are unavailable here. The machine report explicitly marks
full-stage completion and target/hardware validation false. Historical inherited
Z015 evidence remains historical; this is not a newly rerun full Z015 campaign.

## Target commands and open closeout

```powershell
west build -p always -b der26_ams app -d build/z022_publication -- "-DEXTRA_CONF_FILE=z022_measurement_validation.conf"
py scripts/check_all_contracts.py . build/z022_publication
```

Rebuild earlier profiles for exclusion/regression. On hardware, compare current,
cell and temperature values, measure lock wait/ADC/acquisition/publication timing,
inspect calibration/uncertainty and sequence gaps, and exercise sensor/transport
faults. No source or host result constitutes physical migration evidence.

To close the full original stage, integrate the actual mixed/full-pack acquisition
profile, estimator execution and fault-policy consumers, then qualify genuine
heartbeat timing against those completed workloads. A calibration-loading path
with appropriate provenance is also needed before calibrated estimator input can
be claimed. These missing portions are not hidden behind a green host-test result.
