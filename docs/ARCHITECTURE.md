# DRG27 AMS Zephyr architecture

This document freezes the architectural rules for the DER26 -> DRG27 AMS
migration.  The safety oracle remains DER26 AMS v2.6.27 / FW 0.5.30; Zephyr is
used to replace platform mechanisms, not to redefine AMS safety behavior.

## Layering rule

```text
app/
  orchestration, scheduling, safety-policy ownership
          |
          +-----------------------+
          |                       |
          v                       v
lib/ams_core/                 include/ams_platform/
portable algorithms          platform-neutral interfaces
NO Zephyr/HAL/STM32                 |
                                    v
                              drivers/ams/
                              Zephyr adapters
                                    |
                                    v
                         Zephyr device APIs + Devicetree
                                    |
                                    v
                                STM32F767

Exceptional fatal path:
app fatal policy -> ams_platform/fail_low.h
                 -> boards/.../ams_fail_low_stm32.c
                 -> direct PE0 reset registers
```

Dependencies may point downward in that diagram.  They must not point back up.
In particular, `lib/ams_core` may not include Zephyr, FreeRTOS, HAL, Devicetree,
or board headers.

## What belongs in each layer

### `lib/ams_core`

Owns deterministic AMS behavior:

- estimator / EKF;
- SoP / SoH / fuse observer;
- current conversion, calibration and fault policy;
- current-window integration;
- coherent measurement store;
- thermal fan command policy;
- IMD decoding/freshness logic;
- watchdog feed policy / proactive software-integrity policy;
- future CAN scheduling/state logic where practical.

This code must remain directly host-testable without Zephyr.

### `include/ams_platform`

Contains the narrow interfaces that application code may call to perform
platform actions.  Interface names describe the AMS operation, not the Zephyr
or STM32 implementation.  Current public platform interfaces through Z-015 are:

- `bms_ok.h`;
- `fail_low.h`;
- `current_adc.h`;
- `fan_pwm.h`;
- `imd_capture.h`;
- `watchdog.h`;
- `adbms_spi_lifecycle.h` (startup/status only; no raw transfer surface).

The Z-015 raw ADBMS transfer API is deliberately *not* public: it remains in
`drivers/ams/adbms_spi_internal.h` until the single ADBMS owner/protocol layer
lands. Future CAN interfaces should follow the same narrow-surface principle.

### `drivers/ams`

Owns ordinary Zephyr platform adaptation.  It may use Zephyr device APIs,
Devicetree helpers, synchronization primitives needed by the driver boundary,
and platform error codes.  It must not own product-level safety policy.

Z-015 adds one audited exception inside this layer: `adbms_spi_stm32.c` owns
SPI6 directly through STM32 LL because Zephyr v4.4.0's stock F767 full-duplex
interrupt completion path contains an unbounded BSY wait in ISR context. The
private backend still uses Zephyr for Devicetree, pinctrl, GPIO, RCC clock
control, reset control, timing and IRQ disable/pending-clear. Stock `spi_stm32`
does not own SPI6.

Current implementation sources are compiled as the dedicated Zephyr library
`ams_platform` rather than being listed directly as application sources.  The
library is instantiated from `zephyr/CMakeLists.txt` through this repository's
`zephyr/module.yml`, so `zephyr_library_named()` runs in Zephyr **kernel CMake
mode**.  `app/CMakeLists.txt` must not manually `add_subdirectory(drivers/ams)`
after `find_package(Zephyr)`.

### `boards/drexel/der26_ams`

Owns physical board description and the one approved direct-register escape
hatch.  The normal wiring contract lives in Devicetree.  Direct MCU register
access is restricted to `ams_fail_low_stm32.c`, because BMS_OK must be forced
LOW before the kernel/driver stack is available and after a fatal kernel error.

The PRE_KERNEL_1 registration for that emergency primitive is also board-owned;
application orchestration does not own the physical PE0 initialization method.
Zephyr automatically processes the selected board's `CMakeLists.txt`; the app
must never add the board directory manually, which would instantiate the board
safety target twice.

### `app`

Owns:

- static thread topology and priority/order;
- runtime heartbeat accounting;
- safety-supervisor orchestration;
- composition of portable policy with platform interfaces;
- fatal-policy ordering.

It must not include Zephyr ADC/PWM/GPIO/CAN/SPI/watchdog driver headers or own
Devicetree pin/controller mappings.  Critical periodic AMS actors remain
explicit threads rather than generic system-workqueue items.

## Devicetree policy

Application-facing hardware contracts use typed custom bindings rather than
`/zephyr,user`.  Z-015 retains:

- `drexel,ams-safety-io`;
- `drexel,ams-adbms-interface`;
- `drexel,ams-current-sense`;
- `drexel,ams-fan-bank`;
- `drexel,ams-imd`.

Bindings inherit `base.yaml`, so standard properties such as `status` remain
schema-validated.  Adapter code consumes named `adc_dt_spec`, `pwm_dt_spec`, or
`gpio_dt_spec` objects and adds compile-time assertions for frozen DER26 wiring.
The Z-015 typed ADBMS node additionally owns SPI6 pinctrl, manual CS GPIOs,
reset metadata and frozen transport constants while the stock `&spi6` device
node remains disabled.

The Devicetree metadata does not replace behavior that must remain cycle-exact.
For example, fan consumer period metadata is descriptive while the adapter uses
`pwm_set_cycles(..., 3361, ...)` to preserve the v2.6.27 ARR=3360 contract.

## Kconfig and capability policy

User-facing Kconfig controls release-evidence/authority claims.  Hidden
`AMS_CAP_*` symbols describe actual migration facts.  Four states must remain
separate:

1. an adapter exists;
2. the application actor is live;
3. that actor may count as safety heartbeat evidence;
4. physical target validation has been completed.

Code presence never implies safety evidence, and software evidence never implies
physical validation.  BMS_OK assertion and balancing authority remain separate
hard gates.

## Safety-specific exceptions

Using more Zephyr is not itself the goal.  Zephyr is preferred for platform
mechanisms only where it preserves or strengthens the frozen safety semantics.
Therefore:

- the PE0 emergency fail-low primitive remains direct-register board code;
- the coherent measurement store is not replaced by generic zbus messaging;
- critical periodic actors are not moved to a shared system workqueue;
- watchdog feed policy remains AMS-owned even though hardware access will use
  Zephyr's watchdog API;
- future CAN bus-off/on-wire authority semantics remain AMS-owned even though
  transport uses Zephyr CAN.

## Mandatory architecture regression gate

Every target build must run:

```text
py scripts/check_all_contracts.py . <build-dir>
```

`check_architecture_contract.py` prevents core/platform dependency leaks,
legacy HAL/FreeRTOS dependencies, dynamic allocation, direct-register spread,
untyped hardware contracts, and critical system-workqueue migration.
`check_capability_contract.py` separately proves the stage capability matrix.

## Deferred refactors

The following are intentionally deferred until they reduce migration risk:

- split `ams_threads.c` actor bodies into individual task translation units as
  each placeholder becomes a real actor;
- convert non-safety `printk()` diagnostics to Zephyr logging after core safety
  actors are migrated;
- introduce multi-instance adapter contexts only if a real multi-instance or
  testability requirement appears;
- retained fault/panic logging as its own validated safety-evidence stage.

These are not reasons to destabilize a target-green migration stage.

## Mechanical portability criterion

Source review is not enough to prove the abstraction boundary. `lib/ams_core`
must compile and execute its contract tests in an ordinary host-native CMake
project with **no Zephyr, FreeRTOS, CMSIS, STM32 HAL, board tree, or
`ams_platform` include path available**.

The required mechanical gate is:

```text
python3 scripts/check_null_platform_core.py .
```

It builds the real `lib/ams_core/CMakeLists.txt`, runs CTest, then audits
`compile_commands.json` for leaked platform include paths/defines. GitHub CI
runs the same command independently. If a future core file needs `k_mutex`,
`osDelay`, a HAL handle, DeviceTree, or a board header to compile, the gate must
fail. This is the objective test that the Zephyr migration is buying actual
portability rather than producing Zephyr-shaped FreeRTOS code.

This gate supplements rather than replaces the exact v2.6.27 behavioral
differential and safety contracts. Pure code can still be behaviorally wrong.

## Future difficult abstraction seams

### ADBMS / isoSPI

Z-015 freezes a private synchronous SPI6 transport substrate only. It uses one
wrap-safe absolute 500 ms deadline, Mode 3 at exactly 421,875 Hz, manual PE2/PE4
CS, no SPI IRQ/DMA/async path, and CS-high + RCC reset/reconfigure/readback on
transport failure. Recovery success leaves later operations possible; recovery
failure latches the adapter faulted. Initialization issues no transfer.

The future portable boundary remains command construction, PEC, packet parsing,
chain ordering, cell mapping, diagnostic state machines, and other behavior
that does not require a physical transaction. CS-low is itself electrically
meaningful to the ADBMS6822 chain, so wake/session timing remains a later
protocol/runtime concern. CLI/service access must go through the future single
ADBMS owner request/response path; direct diagnostic transport calls are
forbidden.

### CAN

Portable candidates include frame encoding, scheduler/policy state, counters,
staleness and bus-off escalation rules that can be represented as explicit
events. The transport boundary owns controller state, physical BOFF event
delivery, recovery API semantics and proof of TX completion. In particular,
Zephyr `can_send()` success must never be treated as the v2.6.27 authority
event: protected 0x680-0x687 frames must still be credited only after actual
on-wire completion, and repeated BOFF identity must remain event-sequenced at
the physical event boundary.
