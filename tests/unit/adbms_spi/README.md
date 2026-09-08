# Z-015 ADBMS SPI transport host/SIL

This suite validates the **exact current Z-015 private SPI6 transport code**.
It does not substitute a generic/mock transport for the production sequencing
logic.

## Production code under test

- `drivers/ams/adbms_spi_engine.c`
  - RTOS-independent bounded transaction state machine used by the target
    adapter.
  - One wrap-safe absolute transaction deadline.
  - Full-duplex draining for writes.
  - TX-prefix + `0xFF` read clocks and RX-suffix extraction.
  - Frozen 512-byte bound and 500-ms oracle timeout.
  - CS fail-safe ordering and recovery/terminal-fault transitions.
- `drivers/ams/adbms_spi_stm32.c`
  - Compiled unchanged against the fake Zephyr/STM32 backend for production
    adapter SIL.
  - Verifies startup, exact clock/config readback, IRQ-disabled/pending-clear
    policy, CS routing, timeout recovery, RCC reset/reconfigure, and terminal
    recovery failure.

The fake backend supplies only the external Zephyr/STM32 primitives.  It does
not replace the production adapter state machine or production SPI sequencing.

## Canonical directed/model evidence

`make test` includes:

- directed transaction-engine boundary tests;
- five optimization builds (`-O0`, `-O1`, `-O2`, `-O3`, `-Os`), each running
  200,000 randomized model operations: **1,000,000 operations total**;
- production-adapter directed scenarios:
  nominal, TX timeout, RX timeout, BSY timeout, ordinary fault, CS assertion
  failure, recovery failure, clock mismatch, pinctrl failure, and reset-init
  failure;
- production-adapter randomized campaign of 25,000 transactions; the current
  campaign reports **250,013 adapter checks** with zero failures.

`make strict` recompiles the engine and production adapter with additional
conversion/sign/shadow warnings enabled.

`make asan` and `make ubsan` run directed and randomized engine/adapter cases
under AddressSanitizer and UndefinedBehaviorSanitizer. `make analyze` runs GCC
`-fanalyzer`; `make clang-analyze` runs the Clang static analyzer.

The repository-level canonical runner additionally executes the Z-014 safety
regressions and the Z-015 contract mutation suite.  The mutation suite must
reject stock SPI6 ownership, generic SPI, SPI async, RTIO, DMA, wrong wiring or
clocking, Mode-0 drift, IRQ enablement, lost pending-IRQ clearing, polling
scheduler/yield changes, stock `spi_transceive()`, recovery-reset removal,
private API leakage, authority/evidence promotion, and wake-functionality
introduction.

## Explicit non-scope

This suite deliberately does **not** emulate or validate:

- ADBMS6822 wake/isoSPI behavior;
- PEC10/PEC15;
- ADBMS6830/APM commands or register semantics;
- cell/temperature acquisition;
- open-wire/redundant ADC;
- balancing;
- physical SPI waveform timing.

Those remain later migration/hardware gates.  Z-015 has zero runtime transfer
callers, so target initialization must emit no SPI clocks and no CS-low/wake
pulse.
