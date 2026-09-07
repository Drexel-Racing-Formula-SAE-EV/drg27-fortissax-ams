> **Historical target-build note.** The compile fixes documented below were
> required to make the original Z-013 candidate build.  The later Z-013
> architecture hardening replaces the temporary `/zephyr,user`/positional
> consumer approach with typed custom AMS Devicetree bindings and named
> `dt_spec` interfaces.  See `Z013_ARCHITECTURE_HARDENING.md` for current
> architecture.

# Z-013 first STM32 target-build correction

Date: 2026-09-07

The first real Zephyr 4.4 STM32F767 build reached C compilation and exposed two
compile-time devicetree integration defects. Neither defect changes the frozen
v2.6.27 current-sensor or IMD behavior.

## 1. Current ADC `*_BY_IDX` token bug

The adapter used:

```c
#define CURRENT_ADC_HIGH_INDEX 0U
#define CURRENT_ADC_LOW_INDEX  1U
```

Zephyr devicetree `*_BY_IDX` macros concatenate the preprocessor index token
into generated identifier names. This produced references such as
`...IDX_0U...` and `...IDX_1U...`, while generated devicetree identifiers are
`...IDX_0...` and `...IDX_1...`.

The indexes are now bare preprocessor tokens:

```c
#define CURRENT_ADC_HIGH_INDEX 0
#define CURRENT_ADC_LOW_INDEX  1
```

Physical and behavioral contracts are unchanged:

- HIGH = ADC1_IN3 / PA3 / +/-800 A;
- LOW = ADC2_IN10 / PC0 / +/-50 A;
- acquisition remains HIGH then LOW;
- all bounded 5 ms async/fail-closed semantics are unchanged.

## 2. IMD unbound pseudo-node bug

The initial Z-013 DTS used an unbound pseudo-node containing:

```dts
pwms = <&pwm2 1 0 PWM_POLARITY_NORMAL>;
status-gpios = <&gpioc 5 GPIO_ACTIVE_HIGH>;
```

Zephyr emitted the node in the generated DTS, but without a binding it did not
emit the typed phandle/cell macros required by `PWM_DT_SPEC_GET()` and
`GPIO_DT_SPEC_GET()`. The compiler therefore referenced nonexistent
`..._PH_ORD`, `..._VAL_channel`, `..._VAL_period`, and GPIO cell identifiers.

Z-013 now uses already-bound devicetree objects directly:

- M_HS PWM capture device = `DT_NODELABEL(pwm2)`;
- capture channel = 1;
- TIM2 pinctrl remains PA5 / TIM2_CH1;
- OK_HS is `imd-status-gpios` on `/zephyr,user`;
- OK_HS remains PC5, active high.

This follows the same typed board-contract pattern already used for BMS_OK and
removes the unbound-node dependency entirely.

## Validation after correction

- current sensor: 76 checks PASS;
- current fault: 22 checks PASS;
- production current ADC adapter SIL: 516,547 checks PASS;
- current ADC timeout/ambiguous-completion cases: PASS;
- IMD portable core: 57 checks PASS;
- production IMD capture adapter: 776,535 checks PASS;
- fan core: 688 checks PASS;
- fan PWM adapter: 150,679 checks PASS;
- measurement store: 8/8 PASS;
- current window: 9/9 PASS;
- estimator: 89 PASS;
- GCC `-fanalyzer` on both corrected adapters: PASS;
- Clang analyzer on both corrected adapters: PASS;
- Z-013 FreeRTOS runtime/safety parity contract: PASS.

The next required evidence is the real STM32F767 target build and complete
Z-001 through Z-013 build-contract suite. Physical IMD validation remains
separate and `CONFIG_AMS_IMD_TARGET_VALIDATED` must remain disabled.
## 3. Current-sensor target contract stale symbol names

The first complete real-target contract run compiled and linked successfully,
but `check_current_sensor_contract.py` failed on:

```text
current_sensor_calibration_record_crc32
```

A follow-up source/API audit found that this name and the next checker entry,
`current_sensor_restore_calibration`, do not exist in either the frozen
v2.6.27 current-sensor API or the approved Z-011 portable adaptation. They were
stale checker-only names from an earlier planning/synthetic-map assumption.
No production current-sensor behavior was missing.

The checker now separates the two things it actually needs to prove:

- the entry points deliberately exercised by the target startup smoke must be
  present in the target map;
- calibration-record and latch-clear APIs remain locked by exact source hashes,
  host differential/SIL, and explicit source-name checks, but are not required
  to survive target linker garbage collection until the live current-task
  integration stage.

No C production source, DTS, Kconfig, timing, calibration format, fault policy,
ADC behavior, authority gate, or target ELF behavior is changed by this fix.
The already-successful STM32F767 build therefore remains valid evidence; only
the corrected contract script must be rerun against that build.
## Architecture-hardening generated-DTS string-list checker correction

The architecture-hardened STM32F767 image built and linked successfully, but
the unified contract suite stopped in `check_board_contract.py` on the current
ADC `io-channel-names` check. The custom `drexel,ams-current-sense` binding had
already accepted the source DTS and the production adapter selected channels by
`ADC_DT_SPEC_GET_BY_NAME(..., high/low)`, so this was not a hardware mapping or
firmware defect.

The checker had coupled the semantic contract to one exact pretty-printed
`zephyr.dts` layout (`io-channel-names = "high", "low"` on one line). Zephyr may
format string lists across lines. The board/current-ADC checks now parse the
string-list property and compare the resulting ordered values. The board fan
`pwm-names` check was hardened at the same time to prevent the same false
failure later in the unified gate.

No production C/H, DTS, binding, CMake, Kconfig, timing, pin assignment,
authority state, or target ELF behavior changed. The already successful
architecture-hardened target build remains valid evidence; only the Python
contracts need to be rerun against it.

## Architecture-hardening IMD reviewed-hash closeout

The first complete contract run of the architecture-hardened Z-013 target
reached the IMD contract after architecture, board, authority, runtime, core,
measurement, current-window, estimator, power, current, ADC and fan contracts
all passed. The IMD checker then reported `imd_capture.h drifted`.

The production source had not drifted. During the architecture-hardening move
from a driver-private header to the public `include/ams_platform/imd_capture.h`
interface, two reviewed-hash placeholders were accidentally left unresolved in
`check_imd_capture_contract.py`. They are now frozen to the actual reviewed
architecture-hardened files:

- `include/ams_platform/imd_capture.h`: `6774284eeb6a14262f12bea562f9cb02fa483df45f364c16bc59efadc1226cc3`
- `drivers/ams/imd_capture_zephyr.c`: `8a6f31bdd5aef5261a3fcbcb07b3272b638a4a12a9c657e5f5f075402f2a2353`

No production C/H implementation, DTS, binding, Kconfig, CMake, timer/capture
behavior, safety policy or target ELF changed. The existing 187/187 STM32F767
target build therefore remains valid evidence; only the corrected contract
script needs to be rerun against that build.
