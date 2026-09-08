# Z-015 SPI6 Transport Oracle

Status: frozen software migration contract for Z-015.  This document does not authorize ADBMS6822 wake, isoSPI protocol work, ADBMS6830/APM protocol, acquisition, balancing, or vehicle authority.

## Behavioral oracle

The reference implementation is **DER26 AMS v2.6.27 / FW 0.5.30**.  Relevant source is the frozen FreeRTOS tree under `Core/`.

| Property | v2.6.27 source | Frozen Z-015 value |
|---|---|---|
| MCU | board / Cube configuration | STM32F767ZIT6 |
| peripheral | `Core/Src/main.c::MX_SPI6_Init()` | SPI6 |
| SCK / MISO / MOSI | board contract | PG13 / PG12 / PG14 |
| CS A / CS B | board contract | PE2 / PE4, active low |
| master / direction | `MX_SPI6_Init()` | master / 2-line full duplex |
| data size | `MX_SPI6_Init()` | 8 bit |
| polarity / phase | `MX_SPI6_Init()` | CPOL high / CPHA second edge (Mode 3) |
| NSS | `MX_SPI6_Init()` | software |
| bit order | `MX_SPI6_Init()` | MSB first |
| TI / CRC / NSS pulse | `MX_SPI6_Init()` | disabled / disabled / disabled |
| SYSCLK | `SystemClock_Config()` | 216 MHz |
| APB2 divider | `SystemClock_Config()` | HCLK / 2 |
| SPI6 input clock | derived | 108 MHz |
| prescaler | `Core/Inc/ams_build_profile.h` default `AMS_ADBMS_SPI_PRESCALER_DIV` | /256 |
| achieved SCK | 108 MHz / 256 | exactly 421,875 Hz |
| transfer timeout | `Core/Inc/ext_drivers/adbms_shared.h::SPI_TIMEOUT` | 500 ms |
| max shared transfer buffer | `adbms_shared.h::BUFSZ` | 512 bytes |
| read dummy byte | `adbms6830_spi_write_read()` | 0xFF |

The 500 ms timeout is retained for initial parity.  It is deliberately **not** claimed to be an optimal final deadline.  At 421,875 bit/s, 512 bytes take about 9.71 ms of raw wire time.  Tightening the timeout is deferred until later measured protocol/timing evidence exists.

## Exact transfer semantics being preserved

`adbms6830_spi_write()` holds the complete logical SPI operation under the ADBMS recursive lock, optionally drives the selected CS low, calls `HAL_SPI_Transmit(..., SPI_TIMEOUT)`, restores CS high, and releases the lock.

`adbms6830_spi_write_read()` rejects a total transfer exceeding `BUFSZ`, fills a static transmit/receive buffer with 0xFF dummy bytes, overlays the TX prefix, performs one full-duplex `HAL_SPI_TransmitReceive()` across `tx_len + rx_len`, restores CS, and copies only the RX suffix on success.  The caller RX buffer is zeroed on failure.

A HAL timeout/error is an **operation failure, not permanent transport death**.  Higher-level scan/fault policy decides how repeated bad scans affect BMS state, and later healthy operations may recover.  Z-015 therefore attempts bounded peripheral reset/reconfiguration after a transport fault.  The adapter becomes terminal `FAULTED` only if the recovery itself cannot re-establish its hardware contract.

## Mutex semantics and deliberate Zephyr architecture change

FreeRTOS owns a statically allocated recursive, priority-inheriting ADBMS mutex in `Core/Src/app.c`.  It is held for the complete logical operation because ADBMS helpers are nested and shared driver buffers are prepared before the final HAL call.  The tree also tracks owner, recursion depth, wait time, hold time, and violations.

Z-015 does **not** port this multi-caller mutex.  The Zephyr architecture freezes a stronger ownership rule: one future ADBMS owner context is the only legal transport caller.  CLI/service/diagnostic work must request operations through that owner rather than calling the transport directly.  Z-015 itself has **zero runtime transfer callers**; startup only initializes the transport substrate.

## CS is not harmless bookkeeping

ADBMS6822/isoSPI wake behavior uses deliberate CS-low timing (`WAKEUP_US_DELAY` is 1000 us in `adbms_shared.h`).  Therefore any CS assertion is electrically/protocol meaningful.  Z-015 initialization must finish with PE2 and PE4 physically high, SPI6 disabled, and must issue no transaction, no wake pulse, and no protocol command.  A target "smoke-test transfer" is outside Z-015.

## Z-015 capability boundary

Allowed to become true:

- ADBMS SPI adapter present.

Must remain false:

- ADBMS SPI physically validated;
- ADBMS actor live;
- ADBMS safety evidence;
- temperature safety evidence;
- BMS authority;
- balancing authority.

No wake API, PEC, ADBMS6822 protocol, ADBMS6830/APM protocol, cell/temperature acquisition, open-wire, redundant ADC, balancing, or service SPI is part of Z-015.
