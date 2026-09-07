# Z-013 architecture hardening before Z-014

Status: architecture-only hardening of the target-green Z-013 IMD candidate.
No estimator, current-sensor, current-window, SoP/SoH/fuse, fan thermal-policy,
or IMD decode algorithm is intentionally changed.

## Why this work exists

Before adding IWDG in Z-014, the migration was rescoped against the original
architecture objective: AMS application/policy should be as independent of the
MCU and hardware representation as practical, while Zephyr should own normal
platform mechanisms.  The review found the underlying design sound but found
four areas worth fixing before more peripherals are added:

1. application code still included implementation-named `*_zephyr.h` headers;
2. ordinary BMS_OK GPIO ownership still lived in `app/ams_safety.c`;
3. fan/current/IMD hardware descriptions were not yet expressed as first-class
   typed AMS Devicetree consumers;
4. migration status and contract execution were distributed across many manual
   assumptions/scripts.

## Implemented changes

### Public platform interface boundary

Application code now includes only `include/ams_platform/*` interfaces.  Zephyr
implementation filenames remain under `drivers/ams`, but implementation details
are no longer part of the app-facing API.

### Dedicated Zephyr platform library

`drivers/ams` is compiled as the Zephyr library `ams_platform`.  It is created
from `zephyr/CMakeLists.txt` through the repository module metadata while Zephyr
is still in kernel CMake mode.  The selected DER26 board is compiled separately
as `ams_board_safety` by Zephyr's normal board integration; the application does
not manually add the board directory.  The portable `ams_core` dependency
direction is explicit: platform may consume core types and functions; core never
consumes platform code.

### BMS_OK ownership split

Normal GPIO setup moved to `drivers/ams/bms_ok_zephyr.c`.  It exposes only
`ams_bms_ok_platform_init_low()` in the no-authority stage.  It calls the direct
fail-low primitive before normal GPIO ownership and repeats it on every GPIO
error.

The direct RCC/GPIOE implementation and PRE_KERNEL_1 registration now both live
in `boards/drexel/der26_ams/ams_fail_low_stm32.c`.  This is the only approved
production direct-register owner.

### Typed AMS Devicetree bindings

Custom repository bindings were added for safety I/O, future ADBMS chip selects,
dual-range current sensing, six-zone fans, and IMD.  `/zephyr,user` is no longer
used as an AMS hardware contract.

Current ADCs are selected by names `high` and `low`; fans by `fan1`..`fan6`; IMD
by a typed PWM+GPIO consumer.  Compile-time assertions still lock the exact
DER26 controllers, channels, GPIO pins and polarity.

### Explicit migration capabilities

Hidden Kconfig capabilities now distinguish adapter presence, live actor,
safety evidence and physical validation.  Z-013 explicitly records:

- BMS_OK platform adapter present;
- current ADC adapter present, current actor/evidence not live;
- ADBMS SPI/actor/evidence absent;
- CAN adapter/actor/evidence absent;
- fan PWM adapter + actor + software evidence live, physical validation open;
- IMD capture adapter + actor + software evidence live, physical validation
  open;
- watchdog adapter/active/full-oracle coverage absent.

### Unified release gate

`check_all_contracts.py` is now the canonical target gate.  It runs the full
stage contract set, FreeRTOS runtime parity, manifest generation, and Git
whitespace validation from one command.

### Architecture contract

A new architecture checker fails the build gate if:

- Zephyr/HAL/RTOS dependencies enter `ams_core`;
- legacy FreeRTOS/HAL dependencies enter production code;
- application code consumes hardware driver headers or Devicetree mappings;
- direct STM32 register access spreads outside the reviewed board primitive;
- dynamic allocation appears in production;
- critical actors move to the generic system workqueue;
- `/zephyr,user` returns for AMS hardware;
- typed/named dt_spec consumption is removed;
- repository bindings are not actually discovered by Zephyr.

## Safety impact

These changes are intentionally architecture-preserving.  In particular:

- BMS_OK authority remains compile-time impossible;
- balancing authority remains compile-time impossible;
- emergency PE0 fail-low behavior is unchanged and still bypasses normal driver
  dependencies;
- current actor remains deferred;
- CAN and SPI6 remain disabled;
- watchdog remains disabled until Z-014;
- fan and IMD physical validation claims remain false.

The normal BMS_OK path is now *more* constrained architecturally: its public
platform API has no high/assert operation at all.

## Deeper rescope findings

### Do before authority can ever be enabled

1. **Z-014 watchdog:** portable feed policy + Zephyr watchdog adapter + explicit
   evidence mask; no generic watchdog callback may own AMS policy.
2. **Current integration:** reproduce the exact 20 ms transaction, bounded
   priority-inheritance current-window lock, publication ordering and immediate
   fail-low behavior before current can become safety evidence.
3. **ADBMS/temperature:** use native Zephyr SPI/GPIO Devicetree transport,
   preserve the 500 ms shared-SPI mutex budget and exact freshness/recovery
   semantics; temperature evidence cannot be inferred from fan liveness.
4. **CAN:** use Zephyr CAN transport but preserve the hardened repeated bus-off
   event sequence and protected 0x680-0x687 *on-wire* completion authority rule.
5. **Retained fault/panic evidence:** port or replace with equivalent validated
   reset/panic evidence before final safety release.
6. **Full supervisor readiness/authority policy:** only after every required
   oracle heartbeat has real evidence and all physical gates are satisfied.

### Improve incrementally, not by broad refactor now

- `ams_threads.c` is the remaining application concentration point.  Move an
  actor into its own file when its placeholder becomes real, so each move can be
  checked against the corresponding FreeRTOS task rather than performing a
  high-risk mechanical split now.
- Convert diagnostics to `LOG_*` after safety-critical actor migration.  Never
  make safety actions depend on logging.
- Keep the dedicated measurement store and explicit critical threads; replacing
  them with zbus/shared workqueues would alter already-validated semantics with
  little safety benefit.

## Target-build requirement

This hardening changes Devicetree bindings, generated macros and the CMake
library graph.  Previous Z-013 ELF evidence does **not** close this hardening.
A clean STM32F767 Zephyr 4.4 rebuild and the unified contract gate are required
before this repository is considered architecture-hardened/target-green.

## Null-platform and schema hardening closeout

The architecture review was strengthened after the initial WIP in three ways.

1. `tests/null_platform` is an ordinary host CMake project that adds the real
   `lib/ams_core` target directly and has no Zephyr package or platform include
   tree. `scripts/check_null_platform_core.py` performs a pristine configure,
   build, CTest run, and compile-command audit.
2. `.github/workflows/portable-core-null-platform.yml` makes this a mechanical
   CI invariant instead of a convention. The unified Z-013 gate also invokes
   the same checker.
3. The custom current/fan bindings use Zephyr 4.4's supported `const` schema
   form to freeze semantic names (`high`,`low` and `fan1`..`fan6`). Exact
   phandle-array counts remain C `BUILD_ASSERT` contracts because Zephyr 4.4
   does not support the later `min-len`/`max-len` binding keywords.

The fan host fake-Devicetree surface was also corrected to model the numeric
`pwm3`/`pwm4`/`pwm5` node identifiers used by the production adapter's
`DT_SAME_NODE` assertions. That was a host-harness defect, not a production fan
policy change.

This hardening still does not make the current actor live, enable SPI6/CAN1,
enable IWDG, grant BMS/balancing authority, or claim fan/IMD physical
validation. Those capability distinctions remain explicit.

## Target CMake integration correction discovered during first closeout build

The first real `z013_arch_hardened` target configure exposed two build-graph
ownership mistakes in the architecture WIP rather than a firmware/source
failure:

1. `app/CMakeLists.txt` manually added `boards/drexel/der26_ams` even though
   Zephyr had already processed the selected board CMake file.  That attempted
   to create `ams_board_safety` twice and CMake stopped on the duplicate target.
2. `app/CMakeLists.txt` also manually added `drivers/ams` after
   `find_package(Zephyr)`.  At that point Zephyr is in application CMake mode,
   so `zephyr_library_named(ams_platform)` emitted the explicit Zephyr warning
   that the target would not be treated as a Zephyr library.

The corrected ownership model is now:

```text
Zephyr module discovery
  -> zephyr/module.yml
  -> zephyr/CMakeLists.txt        [kernel CMake mode]
  -> drivers/ams/CMakeLists.txt
  -> ams_platform

Zephyr board discovery
  -> boards/drexel/der26_ams/CMakeLists.txt
  -> ams_board_safety             [exactly once]

app/CMakeLists.txt
  -> app sources/orchestration
  -> portable ams_core
  -> link app/platform -> ams_core
  -> never manually add drivers/ams or the board directory
```

The driver and board CMake files now derive the repository root from their own
`CMAKE_CURRENT_LIST_DIR`, so they do not depend on an app-scope variable for
include paths.  The architecture contract rejects regression to the former
manual composition model.
