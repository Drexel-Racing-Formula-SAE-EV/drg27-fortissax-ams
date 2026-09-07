# BMS_OK Zephyr adapter SIL

Compiles the production `drivers/ams/bms_ok_zephyr.c` against a minimal fake
Zephyr GPIO surface. It proves the normal platform path can only establish
physical LOW, invokes the board emergency fail-low primitive before normal GPIO
ownership, repeats fail-low on readiness/configure/write errors, and exposes no
BMS_OK assertion API in the migration stage.
