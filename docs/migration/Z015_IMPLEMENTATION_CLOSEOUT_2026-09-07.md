# Z-015 Private SPI6 Transport — Software Closeout

Date: 2026-09-07

Status: **host/source/SIL GREEN; STM32F767 target rebuild required before target-green claim.**

This closeout is limited to Z-015. It does not perform physical SPI/IWDG tests and does not begin Z-016 ADBMS6822/isoSPI protocol work.

## Frozen boundary

The behavioral reference remains DER26 AMS v2.6.27 / FW 0.5.30. Z-015 adds only the ADBMS SPI6 transport substrate and its lifecycle/status integration. BMS_OK assertion authority and balancing authority remain disabled. ADBMS actor liveness, ADBMS safety evidence, temperature safety evidence, and physical SPI validation remain false.

Z-015 intentionally performs **zero runtime SPI transfers**. Startup prepares the private SPI6 backend and leaves both manual chip selects inactive/high. No wake pulse, PEC, ADBMS6822 protocol, ADBMS6830/APM protocol, acquisition, balancing, or service/CLI SPI operation is present.

## Architecture implemented

The audited Zephyr v4.4.0 STM32 SPI transaction engine is not used for SPI6. The exact upstream file is `drivers/spi/spi_stm32.c`; for STM32F767 8-bit full-duplex master operation its interrupt completion path contains a BSY wait that has no applicable software escape for this part/mode. `spi_release()` is also not an abort primitive. Z-015 therefore uses a small private STM32F767 bounded-polling backend while retaining Zephyr for Devicetree, pinctrl, GPIO, clock-control, reset-control, timing, and build/configuration infrastructure.

Implemented invariants:

- stock `&spi6` Devicetree device remains disabled;
- current image explicitly keeps `CONFIG_SPI=n`, `CONFIG_SPI_ASYNC=n`, `CONFIG_SPI_RTIO=n`, and `CONFIG_SPI_STM32_DMA=n`;
- private backend selects only STM32 LL SPI support and Zephyr reset-controller support;
- SPI6 pins are PG13 SCK / PG12 MISO / PG14 MOSI;
- manual CS A/B are PE2/PE4 active-low;
- SPI6 input clock contract is 108 MHz, prescaler /256, achieved SCK exactly 421,875 Hz;
- Mode 3, 8-bit, MSB-first, full duplex, software NSS;
- one wrap-safe absolute 500-ms transaction deadline, retained from v2.6.27 for parity;
- SPI6 IRQ is disabled and pending state cleared at initialization/recovery boundaries; no SPI6 interrupt transfer path exists;
- no DMA, RTIO, async callback, `k_poll_signal`, stock `spi_transceive()`, heap allocation, scheduler lock, IRQ lock, or inter-byte sleep/yield is used;
- logical writes still drain RX because SPI6 remains full duplex;
- failure restores both CS lines inactive before recovery;
- recovery resets SPI6 through RCC, reapplies the exact hardware contract, and verifies configuration readback;
- successful recovery returns an operation failure while leaving the adapter READY for later operations, preserving v2.6.27 recoverability at the transport boundary;
- failed recovery latches the adapter FAULTED;
- raw transfer API remains private under `drivers/ams/`; application/CLI/diagnostics have no direct transfer access;
- later CLI/service migration is gated on a single-owner ADBMS request/response path.

## Host/SIL depth

The Z-015 SPI suite executes the exact portable transaction engine and compiles the exact production `adbms_spi_stm32.c` against a fake external Zephyr/STM32 backend.

Focused SPI evidence includes:

- directed engine boundary tests;
- optimization/model campaign across `-O0`, `-O1`, `-O2`, `-O3`, and `-Os`: **1,000,000 randomized transport operations**;
- production-adapter directed scenarios for nominal transfer, TX timeout, RX timeout, BSY timeout, ordinary fault, CS failure, recovery failure, clock mismatch, pinctrl failure, and reset-init failure;
- production-adapter randomized campaign: **25,000 transactions / 250,013 checks, zero failures**;
- strict warning build;
- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- GCC `-fanalyzer`;
- Clang static analyzer.

The repository canonical runner also reruns retained Z-001..Z-014 current-scope regressions, null-platform portable-core isolation, watchdog ThreadSanitizer evidence, the integrated Z-014 safety SIL, and both safety-contract mutation suites.

The Z-015 mutation suite rejects **27 intentionally unsafe mutations**, including stock SPI6 ownership, generic SPI enablement, SPI async, RTIO, DMA, wiring/clock/prescaler/mode drift, IRQ enablement, pending-clear removal, polling-yield insertion, stock `spi_transceive()`, recovery-reset removal, raw API leakage, direct application transport access, premature actor/evidence/physical-validation promotion, and wake-functionality introduction. The retained Z-014 mutation suite separately rejects 18 unsafe runtime/watchdog/authority changes.

## Canonical closeout run

Command:

```text
python scripts/run_z015_host_validation.py . --require-clang --tsan \
  --report docs/migration/evidence/Z015_DEEP_HOST_SIL_CANONICAL_REPORT_2026-09-07.json
```

Result:

```text
PASS: complete Z-015 deep host/source/SIL validation
Skipped evidence: none
```

Machine-readable report:

- success: `true`
- stages: **53/53 passed**
- elapsed time: **72.74 s**
- skipped evidence: **none**
- ThreadSanitizer requested: `true`
- ThreadSanitizer performed: `true`
- frozen SoP/SoH/fuse source identity checked: `true`
- integrated Z-014 regression system SIL performed: `true`
- target build performed: `false`
- hardware validation performed: `false`
- later migration stage performed: `false`

Evidence files:

- `docs/migration/evidence/Z015_DEEP_HOST_SIL_CANONICAL_2026-09-07.log`
- `docs/migration/evidence/Z015_DEEP_HOST_SIL_CANONICAL_REPORT_2026-09-07.json`

At closeout generation the evidence SHA-256 values are:

```text
4e5d49c0dab9f2420068be27fac56b5718c63074c47b1b524051c28f8c3fc2ea  Z015_DEEP_HOST_SIL_CANONICAL_2026-09-07.log
db3faf6b4b1d3bdbe4bc908a776d21f10a567c271bd41159581b888b895a93b7  Z015_DEEP_HOST_SIL_CANONICAL_REPORT_2026-09-07.json
```

## Remaining Z-015 gates

This source snapshot must now re-earn STM32F767 target status with fresh base and IWDG-enabled builds plus `scripts/check_all_contracts.py` against each build. The previous Z-014 target-green result cannot be inherited because Z-015 changes Devicetree/Kconfig and introduces the private target SPI6 backend.

Physical SPI waveform validation remains open and is not claimed here. No Z-016 migration work should be inferred from this closeout.
