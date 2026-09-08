#!/usr/bin/env python3
"""Target/source contract for the hardened private STM32F767 current ADC path."""

import argparse
import re
from pathlib import Path
import sys


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def block(text: str, label: str) -> str:
    start = text.find(label)
    if start < 0:
        fail(f"missing devicetree block {label}")
    brace = text.find("{", start)
    if brace < 0:
        fail(f"malformed devicetree block {label}")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    fail(f"unterminated devicetree block {label}")


def config_disabled(cfg: str, symbol: str) -> bool:
    return (f"# {symbol} is not set" in cfg) or (f"{symbol}=n" in cfg)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    driver = repo / "drivers" / "ams" / "current_adc_stm32.c"
    header = repo / "include" / "ams_platform" / "current_adc.h"
    binding = repo / "dts" / "bindings" / "ams" / "drexel,ams-current-sense.yaml"
    board = repo / "boards" / "drexel" / "der26_ams" / "der26_ams.dts"
    dot_config_path = build / "zephyr" / ".config"
    dts_path = build / "zephyr" / "zephyr.dts"
    map_path = build / "zephyr" / "zephyr.map"

    for path in (driver, header, binding, board, dot_config_path, dts_path, map_path):
        require(path.is_file(), f"missing {path}")

    d = driver.read_text(encoding="utf-8")
    h = header.read_text(encoding="utf-8")
    y = binding.read_text(encoding="utf-8")
    b = board.read_text(encoding="utf-8")
    cfg = dot_config_path.read_text(encoding="utf-8")
    generated = dts_path.read_text(encoding="utf-8")
    link_map = map_path.read_text(encoding="utf-8")

    # Frozen v2.6.27 electrical/acquisition facts are owned by the typed AMS
    # node while ADC1/ADC2/ADC3 remain disabled as Zephyr ADC devices.
    for token in (
        'compatible = "drexel,ams-current-sense";',
        'high-controller = <&adc1>;',
        'low-controller = <&adc2>;',
        'pinctrl-0 = <&adc1_in3_pa3 &adc2_in10_pc0>;',
        'resets = <&rctl STM32_RESET(APB2, 8)>;',
        'reset-names = "adc-common";',
        'high-channel = <3>;',
        'low-channel = <10>;',
        'adc-input-clock-hz = <108000000>;',
        'adc-prescaler = <6>;',
        'adc-clock-hz = <18000000>;',
        'resolution-bits = <12>;',
        'acquisition-ticks = <480>;',
        'conversion-timeout-ms = <5>;',
    ):
        require(token in b, f"DER26 current ADC board-contract token missing: {token}")

    require('include:' in y and '- pinctrl-device.yaml' in y and '- reset-device.yaml' in y,
            "current-sense binding must own pinctrl and common-reset metadata")
    for prop, value in (
        ("high-channel", "3"), ("low-channel", "10"),
        ("adc-input-clock-hz", "108000000"), ("adc-prescaler", "6"),
        ("adc-clock-hz", "18000000"), ("resolution-bits", "12"),
        ("acquisition-ticks", "480"), ("conversion-timeout-ms", "5"),
    ):
        require(re.search(rf"{re.escape(prop)}:\s*(?:.|\n)*?const:\s*{value}\b", y) is not None,
                f"binding does not freeze {prop}={value}")

    for token in (
        '#define AMS_CURRENT_ADC_TIMEOUT_MS 5U',
        '#define AMS_CURRENT_ADC_HIGH_CHANNEL 3U',
        '#define AMS_CURRENT_ADC_LOW_CHANNEL 10U',
        '#define AMS_CURRENT_ADC_RESOLUTION_BITS 12U',
        '#define AMS_CURRENT_ADC_ACQUISITION_TICKS 480U',
        '#define AMS_CURRENT_ADC_PRESCALER 6U',
        '#define AMS_CURRENT_ADC_NOMINAL_VREF_MV 3300U',
    ):
        require(token in h, f"current ADC public constant drift: {token}")

    # Private polling architecture: no generic Zephyr ADC driver, async
    # completion object, DMA or ISR owns transaction lifetime.
    for token in (
        '#define CURRENT_ADC_NODE DT_NODELABEL(ams_current_sense)',
        'BUILD_ASSERT(!IS_ENABLED(CONFIG_ADC)',
        'BUILD_ASSERT(IS_ENABLED(CONFIG_USE_STM32_LL_ADC)',
        'DT_IRQN(CURRENT_ADC_HIGH_NODE) == DT_IRQN(CURRENT_ADC_LOW_NODE)',
        'DT_IRQN(CURRENT_ADC_HIGH_NODE) == 18',
        'irq_disable(irq);',
        'NVIC_ClearPendingIRQ((IRQn_Type)irq);',
        'reset_line_toggle_dt(&adc_common_reset)',
        'LL_ADC_SetCommonClock(adc_common, LL_ADC_CLOCK_SYNC_PCLK_DIV6)',
        'LL_ADC_SetResolution(adc, LL_ADC_RESOLUTION_12B)',
        'LL_ADC_REG_SetTriggerSource(adc, LL_ADC_REG_TRIG_SOFTWARE)',
        'LL_ADC_REG_SetContinuousMode(adc, LL_ADC_REG_CONV_SINGLE)',
        'LL_ADC_REG_SetDMATransfer(adc, LL_ADC_REG_DMA_TRANSFER_NONE)',
        'LL_ADC_SetChannelSamplingTime(adc, channel, LL_ADC_SAMPLINGTIME_480CYCLES)',
        'LL_ADC_REG_StartConversionSWStart(adc)',
        'LL_ADC_IsActiveFlag_EOCS(adc)',
        'LL_ADC_IsActiveFlag_OVR(adc)',
        'WRITE_REG(adc->SR, ~(LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS))',
        'elapsed_ms > CURRENT_ADC_TIMEOUT_MS',
        'if (LL_ADC_IsActiveFlag_EOCS(adc) == 0U)',
        'return recover_and_return(-ETIMEDOUT)',
        'adc_contract_readback_valid()',
        'CURRENT_ADC_STATE_FAULTED',
        'atomic_inc_saturating(&integrity_violation_count)',
    ):
        require(token in d, f"private current ADC safety mechanism missing: {token}")

    for forbidden in (
        '#include <zephyr/drivers/adc.h>', 'adc_read(', 'adc_read_dt(',
        'adc_read_async', 'adc_context', 'k_poll(', 'k_poll_signal',
        'irq_enable(', 'IRQ_CONNECT(', 'k_yield(', 'k_sleep(', 'k_sched_lock(',
        'irq_lock(', 'malloc(', 'calloc(', 'k_malloc(',
    ):
        require(forbidden not in d, f"forbidden current ADC ownership/blocking path introduced: {forbidden}")

    require('k_irq_clear_pending' not in d and
            'CONFIG_ARCH_HAS_IRQ_PENDING_OPS' not in d,
            'current ADC uses pending-IRQ API/capability unavailable in Zephyr v4.4.0')

    # Recovery must force the shared IRQ quiescent before reset and again after
    # reset, because RCC ADCRST does not own the NVIC pending latch.
    recovery = d[d.find("static int reset_and_reconfigure_adc"):
                 d.find("static int recover_and_return")]
    first_irq = recovery.find("disable_and_clear_adc_irq();")
    reset = recovery.find("reset_line_toggle_dt(&adc_common_reset)")
    second_irq = recovery.find("disable_and_clear_adc_irq();", first_irq + 1)
    program = recovery.find("program_adc_contract();")
    verify = recovery.find("adc_contract_readback_valid()")
    require(0 <= first_irq < reset < second_irq < program < verify,
            "ADC recovery order must be IRQ-off/clear -> reset -> IRQ-off/clear -> reprogram -> verify")

    # Frozen HIGH->LOW order, and HIGH failure suppresses LOW.
    high_pos = d.find("current_adc_read_one(adc_high")
    low_pos = d.find("current_adc_read_one(adc_low")
    require(high_pos >= 0 and low_pos > high_pos,
            "ADC acquisition order must remain HIGH then LOW")
    high_fail = d.find("if (ret != 0)", high_pos)
    high_return = d.find("return ret;", high_fail)
    require(high_fail >= 0 and high_return < low_pos,
            "HIGH ADC failure must suppress LOW acquisition")

    # Deferred integration remains unchanged: startup initializes the adapter,
    # but the current worker is not promoted in this migration pass.
    threads = (repo / "app" / "src" / "ams_threads.c").read_text(encoding="utf-8")
    safety = (repo / "app" / "src" / "ams_safety.c").read_text(encoding="utf-8")
    main = (repo / "app" / "src" / "main.c").read_text(encoding="utf-8")
    require("ams_current_adc_read_pair" not in threads,
            "scope creep: live current worker already calls current ADC")
    require("ams_current_adc_read_pair" not in safety,
            "safety layer must not directly own current ADC")
    require("ams_current_adc_read_pair(&" not in main,
            "startup must initialize but not acquire current")
    require("current_adc_read_anchor" in main,
            "target link anchor for deferred current acquisition missing")
    require("ams_current_adc_init()" in main,
            "startup does not fail closed on current ADC init failure")

    # Generated target facts. ADC1/2/3 must remain disabled as Zephyr devices,
    # while the custom AMS node is active and owns the exact scalar contract.
    current_node = block(generated, "ams_current_sense:")
    adc1 = block(generated, "adc1:")
    adc2 = block(generated, "adc2:")
    adc3 = block(generated, "adc3:")
    require('compatible = "drexel,ams-current-sense"' in current_node,
            "generated typed current-sense node missing")
    require('status = "okay"' in current_node,
            "generated typed current-sense node not enabled")
    for token in (
        "high-channel = < 0x3 >;", "low-channel = < 0xa >;",
        "adc-input-clock-hz = < 0x66ff300 >;", "adc-prescaler = < 0x6 >;",
        "adc-clock-hz = < 0x112a880 >;", "resolution-bits = < 0xc >;",
        "acquisition-ticks = < 0x1e0 >;", "conversion-timeout-ms = < 0x5 >;",
    ):
        # Zephyr's generated DTS normally renders integers in hex. Accept
        # decimal too so this gate is not coupled to dtc pretty-print style.
        prop = token.split(" = ")[0]
        require(prop in current_node, f"generated current-sense property missing: {prop}")
    require('status = "disabled"' in adc1, "ADC1 generic Zephyr device must remain disabled")
    require('status = "disabled"' in adc2, "ADC2 generic Zephyr device must remain disabled")
    require('status = "disabled"' in adc3, "ADC3 must remain disabled across common ADC reset")

    require(config_disabled(cfg, "CONFIG_ADC"), "generic CONFIG_ADC must remain disabled")
    require(config_disabled(cfg, "CONFIG_ADC_ASYNC"), "CONFIG_ADC_ASYNC must remain disabled")
    require(config_disabled(cfg, "CONFIG_ADC_STM32_DMA"), "ADC DMA must remain disabled")
    require("CONFIG_AMS_CURRENT_ADC_PRIVATE_BACKEND=y" in cfg,
            "private current ADC backend not enabled")
    require("CONFIG_USE_STM32_LL_ADC=y" in cfg,
            "STM32 LL ADC support not linked for private backend")
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in cfg,
            "current ADC hardening must not enable BMS authority")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in cfg,
            "current ADC hardening must not enable balance authority")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in cfg,
            "current ADC hardening must remain application-heap-free")

    for symbol in (
        "ams_current_adc_init",
        "ams_current_adc_read_pair",
        "ams_current_adc_is_faulted",
    ):
        require(symbol in link_map, f"linked current ADC symbol missing: {symbol}")
    require("adc_stm32_read_async" not in link_map and "adc_stm32_isr" not in link_map,
            "generic STM32 ADC async/ISR implementation unexpectedly linked")

    print("PASS: hardened private STM32F767 current-ADC recovery/parity contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
