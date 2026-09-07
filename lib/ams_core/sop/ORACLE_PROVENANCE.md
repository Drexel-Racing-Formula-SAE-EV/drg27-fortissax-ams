# Z-010 v2.6.27 power-core provenance

Source oracle:

- package: `DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`
- firmware: `0.5.30`
- date: `2026-09-06`

Production files copied into `ams_core`:

| Portable file | Frozen oracle SHA-256 |
|---|---|
| `include/ams_core/ams_soh.h` | `20fe7e29da6531bceb159b38870ce3c4e0a2a0cb84205be2da3531ebf69be719` |
| `soh/ams_soh.c` | `46c9b26444d69d09a82428707f636181a527d624c0734b4ce5aa240f7669a9d4` |
| `include/ams_core/ams_sop.h` | `03a9c889d0b4291dacbd0a521033293e4c8f27442e4f5fd349442295d23b3773` |
| `sop/ams_sop.c` | `ae4898ff09a6295b848a63bc15dd2071deadc722a11c7ec2a0399842f1ce7496` |
| `include/ams_core/ams_fuse_observer.h` | `9f0dcb554860c418af0cb8014299f661e29d8dbb8b508069db73ea95a1c354ab` |
| `sop/ams_fuse_observer.c` | `d0182a133c996487ef81166287ccbb05e2841ad496ece81d7c2bbdd867a163fa` |

Allowed source adaptations are limited to include paths:

- `soh/ams_soh.h` -> `<ams_core/ams_soh.h>`
- `sop/ams_sop.h` -> `<ams_core/ams_sop.h>`
- `estimator/ams_estimator_lut.h` -> `<ams_core/ams_estimator_lut.h>`
- `sop/ams_fuse_observer.h` -> `<ams_core/ams_fuse_observer.h>`

`check_power_core_contract.py` reverses those substitutions before hashing, so
algorithm or constant drift fails the build contract.

Not part of Z-010:

- `ams_power_state`
- `ams_power_strategy`
- `ams_power_can`

Those remain for later application-integration / CAN phases.
