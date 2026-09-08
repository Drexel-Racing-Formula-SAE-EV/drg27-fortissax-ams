# Z-014 watchdog policy oracle provenance

Frozen behavioral oracle: DER26 AMS v2.6.27 / firmware 0.5.30, package
`DER26-AMS-MiL-v2.6.27-ci-build-report-fix-2026-09-06`.

Relevant SHA-256 source identities:

- `Core/Inc/app.h`: `90071c2ab9fef85fac74e89140c70fb9e59aa578c28d2d8c8058c9d98dc6c808`
- `Core/Src/app.c`: `eadae1c3cc16c1867d0a701b2c39322ce597fafd42aa922d0250e732dea31d7e`
- `Core/Inc/ext_drivers/ams_safety.h`: `a8c224084827ce6dd6fde014390a8b211298ddd717a6c7deec96148a3cafce17`
- `Core/Src/ext_drivers/ams_safety.c`: `a33dd7f9853b9fe1c531e9bf7380369607f78d131dd2380e0ae7ee1fde32aecc`
- `Core/Inc/ext_drivers/ams_rtos_diag.h`: `b250b7e61e3c000e0e79b4aeba6c2e35665a2183c24903c18bb95db586c73d20`
- `Core/Src/ext_drivers/ams_rtos_diag.c`: `031f6931fe844b0f494596fd4e01c8e70fa9561c9cb76cd85f3b7a957e16f661`
- `Core/Inc/tasks/error_task.h`: `aa1a7aa44789d1c3b0aed7b706df9eb0d18a7285a590b686ce868d99b96f38e7`
- `Core/Src/tasks/error_task.c`: `206bf8b3f5d8884d050ef9be1ab0e368a377af566cdc2a60b1b9cab23aefc663`

The portable policy retains the stable watchdog block-reason numeric schema,
3000 ms startup grace, software-liveness meaning, process-fault independence,
heartbeat-mask semantics, saturating counters and real-feed-only timestamping.

The Z-014 migration stage deliberately evaluates only real migrated safety
evidence (`FAN | IMD`) while retaining a separate final-oracle required mask.
That partial coverage is explicit and cannot authorize BMS_OK.

STM32/Zephyr mechanism details are not part of this portable core. They are
isolated in `drivers/ams/watchdog_zephyr.c`.
