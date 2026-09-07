# AMS portable core

`ams_core` is the platform-neutral AMS logic layer.

It must not depend on:

- Zephyr kernel or drivers
- FreeRTOS
- CMSIS-RTOS
- STM32 HAL
- STM32 registers
- board pins
- dynamic allocation
- application thread ownership

## Z-006 scope

Z-006 contains only:

- portable scalar types
- frozen physical topology
- the UINT16_MAX unknown-current-uncertainty sentinel
- wrap-safe millisecond age helpers
- deterministic contract checks

Estimator tuning, faults, CAN policy, calibration, battery thresholds, and
driver semantics are migrated separately from the frozen v2.6.27 / FW0.5.30
oracle.