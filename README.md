author: @Mahad-Faisal
WORK IN PROGRESS R&D MAIN REPO IS DER26AMS

Current migration stage: **Z-013 architecture-hardening candidate (post-IMD, pre-Z-014)**. This image remains
compile-time no-authority (`BMS_OK` assertion and balancing disabled) and does
not claim physical IMD target validation.

## Migration architecture invariant

`lib/ams_core` is required to remain host-native portable C: no Zephyr,
FreeRTOS/CMSIS, STM32 HAL, board headers, or `ams_platform` dependency. Normal
hardware access belongs in `drivers/ams` behind narrow `include/ams_platform`
interfaces and typed Devicetree bindings; the only direct-register exception is
the board-owned emergency PE0 BMS_OK fail-low primitive.

Mechanical gate:

```text
python3 scripts/check_null_platform_core.py .
```

For a completed target build, the canonical full stage gate remains:

```text
python3 scripts/check_all_contracts.py . <build-dir>
```
