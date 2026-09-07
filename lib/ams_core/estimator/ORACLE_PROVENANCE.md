# Z-009 estimator oracle provenance

Frozen oracle package:

`DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`

Firmware identity: `0.5.30`

The estimator files below were copied from that package. The portable copies
change only include paths needed to live under `lib/ams_core`.

| Oracle file | SHA-256 |
| --- | --- |
| `AMS/Core/Inc/estimator/ams_soc_ekf.h` | `209e62da3136aa0ccc3ea427560319e13ea2929226a4584b7381131f88db7b77` |
| `AMS/Core/Src/estimator/ams_soc_ekf.c` | `f277f28289f78c016ddf47bcf4fc0c68c0cf72a1d6fda00cc66f62b9cce0d928` |
| `AMS/Core/Inc/estimator/ams_estimator_lut.h` | `c28062bccd31328a32be323df4d0ff33e81d311eb706ee8dee9b56aa950eb7fc` |
| `AMS/Core/Src/estimator/ams_estimator_lut.c` | `c452ce2662694fba0ea5f8361b420f42e9790164ba27528777703b182c8bf93f` |

Allowed source transformations for Z-009:

- `"ams_build_profile.h"` -> `<ams_core/ams_estimator_config.h>`
- `"estimator/ams_soc_ekf.h"` -> `<ams_core/ams_soc_ekf.h>`
- `"estimator/ams_estimator_lut.h"` -> `<ams_core/ams_estimator_lut.h>`

No numerical constant, state field, LUT value, branch condition, covariance
equation, acquisition rule, R0 gate, or estimator API is intentionally changed.
The Z-009 contract checker reverses those include substitutions and verifies the
four oracle SHA-256 values.
