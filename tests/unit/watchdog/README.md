# Z-014 watchdog host validation

This suite validates the portable watchdog policy and proactive stack-headroom
policy without Zephyr, FreeRTOS, CMSIS or STM32 headers.

`watchdog_reference_oracle.c` is a small direct transcription of the pure
software-liveness decision ordering in v2.6.27
`ams_safety_watchdog_task_update()` / `ams_safety_watchdog_ok()`. The 2.5M
comparison campaign intentionally holds the hardware-start mechanism in the
successful state so it compares the genuinely portable policy seam exactly.
Zephyr-specific start ambiguity/feed failure and partial-migration evidence are
tested separately as approved platform adaptations.
