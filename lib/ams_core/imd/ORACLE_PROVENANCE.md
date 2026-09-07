# Z-013 IMD oracle provenance

Frozen oracle package:

`DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`

Firmware identity: `0.5.30`

Relevant exact source hashes:

| Oracle file | SHA-256 |
| --- | --- |
| `AMS/Core/Inc/ext_drivers/imd.h` | `81d8dc354098544fc8164049a831eede20f6a265bbc62a8f9076548e26c43180` |
| `AMS/Core/Src/ext_drivers/imd.c` | `b23265087af40afb06a55d56fde018cef656c9260b4a8ce9c38d43489289f73f` |
| `AMS/Core/Inc/tasks/imd_task.h` | `2fd10284eeb450567ffdfddf4927fe4a1e7c9605faf9787097ab9559e71a0b12` |
| `AMS/Core/Src/tasks/imd_task.c` | `c264f5f773d7280e5227925414736e21bc08fd3f660960ec6e37f1ba3bb3e385` |
| `AMS/Core/Src/board.c` | `5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df` |
| `AMS/Core/Src/main.c` | `ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84` |
| `AMS/Core/Src/stm32f7xx_hal_msp.c` | `60147313c3df238de122e7089331f852960392e67f7f2c9600ab1f810ae37302` |

The portable core reproduces the v2.6.27 status/freshness/duty/frequency and
coherent-capture behavior while replacing HAL timer/GPIO ownership with a
narrow Zephyr adapter.

Z-013 deliberately does **not** claim `AMS_IMD_TARGET_VALIDATED=1`. Physical
polarity/frequency/status validation remains a later hardware gate, and BMS_OK
assertion authority remains compile-time impossible.
