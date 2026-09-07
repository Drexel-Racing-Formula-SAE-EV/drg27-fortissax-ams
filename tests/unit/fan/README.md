# Z-012 fan validation

This suite validates the portable v2.6.27 fan thermal policy and compiles the
actual production `drivers/ams/fan_pwm_zephyr.c` against a fake Zephyr PWM API.
It covers fail-max temperature behavior, hysteresis/charge behavior, exact
3360-CCR mapping, all six physical timer/channel mappings, platform-init fatal
classification, retryable channel failures, and all-zone continuation.
