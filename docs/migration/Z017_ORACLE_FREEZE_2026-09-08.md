# Z-017 FreeRTOS oracle freeze

Date: 2026-09-08

Z017 ports the selected DER26 AMS v2.6.27 / FW 0.5.30 **BENCH Validation 1-SMB** behavior only. The resolved profile is `AMS_BUILD_PROFILE=5`, `AMS_BENCH_VALIDATION_SINGLE_SMB=1`, String B / PE4, one physical ADBMS6830B, 16 register channels read with cells 1..15 monitored.

The exact source hashes used for review are recorded in `docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt`. The hash record covers the selected `adbms6830.c`, `accumulator.c`, `adbms_task.c`, relevant headers, the controlling Z017 plan, and the Z016 input package.

Frozen behavior implemented and contract-checked in Z017:

- production CFGA `81 00 00 FF 03 03`;
- production CFGB `71 52 46 00 00 00`;
- current-board ADCV `03 E0` and startup baseline ADCV `03 64`;
- 3 ms reference pre-wait, 17 ms cell-conversion wait, 10 us SNAP settle;
- raw C authoritative cell product, AVG8 and FC=3 IIR advisory only;
- CCTS must be nonzero for a coherent current-board epoch;
- two POST attempts maximum and two whole-epoch attempts maximum;
- no second ADCV for the whole-epoch retry;
- retained cell validity: 500..5000 mV inclusive, max age 2500 ms, max two consecutive misses, jump threshold 250 mV, stuck threshold 120 unchanged samples;
- String A has no fallback;
- no BMS_OK authority, balance authority, temperature safety evidence, APM/COMM, estimator publication, or ADBMS safety heartbeat.

The selected FreeRTOS `accumulator_init()` also performs a startup emergency balancing inhibit after topology validation. Z017 ports only the fail-safe subset: MUTE, DCC/timer/PWM zeroing and physical readback. `UNMUTE` and nonzero balancing commands are not admitted into the Z017 command inventory.

Intentional safety hardening versus the FreeRTOS oracle is documented rather than hidden: mandatory POST restoration failure terminates initialization instead of permitting a subsequent POST attempt to obscure unproven configuration state; snapshot cleanup obligation is conservative across uncertain mutating outcomes; IIR readiness is explicitly qualified by two complete filtered epochs at least 100 ms apart.
