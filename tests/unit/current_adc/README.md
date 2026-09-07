# Z-011 Current ADC adapter SIL

This host SIL compiles the real `drivers/ams/current_adc_zephyr.c` against a
minimal fake Zephyr ADC/kernel surface. It verifies the adapter state machine
without simulating analog behavior.

Covered invariants:

- readiness failure does not partially initialize the adapter;
- each conversion is reconfigured immediately before use;
- HIGH (ADC1/±800 A) is always acquired before LOW (ADC2/±50 A);
- a HIGH failure suppresses all LOW activity;
- a LOW failure preserves HIGH freshness but never claims a coherent pair;
- setup/start/completion errors are propagated and remain retryable;
- the 5 ms timeout is explicit;
- timeout after an asynchronous start on either HIGH or LOW latches the adapter
  faulted and makes recovery reboot-only, preventing reuse of storage that an
  overdue ADC ISR may still own;
- a logically impossible successful poll without its completion signal is also
  treated as ambiguous in-flight ownership and latched fail-closed.

It intentionally does not emulate STM32 electrical ADC timing. Target timing
and fault-injection remain hardware gates.
