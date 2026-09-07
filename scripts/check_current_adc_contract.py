#!/usr/bin/env python3

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


def string_list_property(node: str, name: str) -> list[str]:
    match = re.search(
        rf"(?:^|\n)\s*{re.escape(name)}\s*=\s*((?:\"[^\"]*\"\s*,?\s*)+);",
        node,
        flags=re.MULTILINE,
    )
    require(match is not None, f"missing generated string-list property: {name}")
    return re.findall(r'\"([^\"]*)\"', match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    driver = repo / "drivers" / "ams" / "current_adc_zephyr.c"
    header = repo / "include" / "ams_platform" / "current_adc.h"
    board = repo / "boards" / "drexel" / "der26_ams" / "der26_ams.dts"
    dot_config_path = build / "zephyr" / ".config"
    dts_path = build / "zephyr" / "zephyr.dts"
    map_path = build / "zephyr" / "zephyr.map"

    for path in (driver, header, board, dot_config_path, dts_path, map_path):
        require(path.is_file(), f"missing {path}")

    d = driver.read_text(encoding="utf-8")
    h = header.read_text(encoding="utf-8")
    b = board.read_text(encoding="utf-8")
    cfg = dot_config_path.read_text(encoding="utf-8")
    generated = dts_path.read_text(encoding="utf-8")
    link_map = map_path.read_text(encoding="utf-8")

    # Source-level immutable physical/electrical acquisition contract.
    source_tokens = (
        '#include <zephyr/dt-bindings/adc/adc.h>',
        'ams_current_sense: ams-current-sense {',
        'compatible = "drexel,ams-current-sense";',
        'io-channels = <&adc1 3>, <&adc2 10>;',
        'io-channel-names = "high", "low";',
        '&adc1 {',
        'pinctrl-0 = <&adc1_in3_pa3>;',
        'st,adc-clock-source = "SYNC";',
        'st,adc-prescaler = <6>;',
        'vref-mv = <3300>;',
        'channel@3 {',
        'reg = <3>;',
        'zephyr,gain = "ADC_GAIN_1";',
        'zephyr,reference = "ADC_REF_VDD_1";',
        'zephyr,acquisition-time = <ADC_ACQ_TIME(ADC_ACQ_TIME_TICKS, 480)>;',
        'zephyr,resolution = <12>;',
        'zephyr,oversampling = <0>;',
        '&adc2 {',
        'pinctrl-0 = <&adc2_in10_pc0>;',
        'channel@a {',
        'reg = <10>;',
    )
    for token in source_tokens:
        require(token in b, f"DER26 Z-011 ADC DTS token missing: {token}")

    for token in (
        '#define AMS_CURRENT_ADC_TIMEOUT_MS 5U',
        '#define AMS_CURRENT_ADC_HIGH_CHANNEL 3U',
        '#define AMS_CURRENT_ADC_LOW_CHANNEL 10U',
        '#define AMS_CURRENT_ADC_RESOLUTION_BITS 12U',
        '#define AMS_CURRENT_ADC_ACQUISITION_TICKS 480U',
        '#define AMS_CURRENT_ADC_PRESCALER 6U',
        '#define AMS_CURRENT_ADC_NOMINAL_VREF_MV 3300U',
    ):
        require(token in h, f"current ADC adapter constant drift: {token}")

    require("#define CURRENT_ADC_NODE DT_NODELABEL(ams_current_sense)" in d,
            "current adapter must consume typed AMS current-sense node")
    require("ADC_DT_SPEC_GET_BY_NAME(CURRENT_ADC_NODE, high)" in d,
            "current high-range ADC must be selected by Devicetree name")
    require("ADC_DT_SPEC_GET_BY_NAME(CURRENT_ADC_NODE, low)" in d,
            "current low-range ADC must be selected by Devicetree name")
    require("CURRENT_ADC_HIGH_INDEX" not in d and "CURRENT_ADC_LOW_INDEX" not in d,
            "current adapter must not depend on fragile positional DT index macros")

    # Adapter design: bounded async acquisition with persistent lifetime-safe
    # storage. The ordinary STM32 synchronous path would wait K_FOREVER in
    # Zephyr 4.4's adc_context, so it is intentionally forbidden here.
    for token in (
        "static current_adc_channel_context_t high_context;",
        "static current_adc_channel_context_t low_context;",
        "adc_channel_setup_dt(spec)",
        "adc_sequence_init_dt(spec, &sequence)",
        "adc_read_async_dt(spec, &sequence, &context->signal)",
        "k_poll(&context->event, 1, K_MSEC(AMS_CURRENT_ADC_TIMEOUT_MS))",
        "context->wedged = true;",
        "adapter_faulted = true;",
        "current_adc_read_one(&current_adc_high",
        "current_adc_read_one(&current_adc_low",
    ):
        require(token in d, f"current ADC safety mechanism missing: {token}")

    require("adc_read_dt(" not in d and "adc_read(" not in d,
            "unbounded synchronous ADC read introduced into Z-011 adapter")
    require("malloc(" not in d and "calloc(" not in d and "k_malloc(" not in d,
            "dynamic allocation introduced into current ADC adapter")

    high_pos = d.find("current_adc_read_one(&current_adc_high")
    low_pos = d.find("current_adc_read_one(&current_adc_low")
    require(high_pos >= 0 and low_pos > high_pos,
            "ADC acquisition order must remain HIGH then LOW")
    high_fail_return = d.find("if (ret != 0)", high_pos)
    high_fail_return_end = d.find("return ret;", high_fail_return)
    require(high_fail_return >= 0 and high_fail_return_end < low_pos,
            "HIGH failure must suppress LOW acquisition")

    # Z-011 is adapter/core only. Do not silently integrate the live safety
    # transaction before Z-022's mutex/order proof.
    threads = (repo / "app" / "src" / "ams_threads.c").read_text(encoding="utf-8")
    safety = (repo / "app" / "src" / "ams_safety.c").read_text(encoding="utf-8")
    main = (repo / "app" / "src" / "main.c").read_text(encoding="utf-8")
    require("ams_current_adc_read_pair" not in threads,
            "Z-011 scope creep: live current thread already reads ADC")
    require("ams_current_adc_read_pair" not in safety,
            "Z-011 scope creep: safety layer directly reads ADC")
    require("ams_current_adc_read_pair(&" not in main,
            "Z-011 startup must initialize but not acquire current")
    require("current_adc_read_anchor" in main,
            "Z-011 target link anchor for deferred ADC acquisition missing")
    require("ams_current_adc_init()" in main,
            "Z-011 startup does not fail closed on ADC-adapter readiness")
    require("ams_current_window_update" not in d and "set_bms" not in d,
            "platform ADC adapter contains product/safety policy")

    # Generated build evidence.
    current_node = block(generated, "ams_current_sense:")
    adc1 = block(generated, "adc1:")
    adc2 = block(generated, "adc2:")
    can1 = block(generated, "can1:")
    spi6 = block(generated, "spi6:")

    require('compatible = "drexel,ams-current-sense"' in current_node,
            "generated typed current-sense node missing")
    require("io-channels" in current_node, "generated Z-011 io-channels missing")
    require(
        string_list_property(current_node, "io-channel-names") == ["high", "low"],
        "generated current channel names/order drift",
    )
    require('status = "okay"' in adc1, "ADC1 not enabled in generated DTS")
    require('status = "okay"' in adc2, "ADC2 not enabled in generated DTS")
    require('status = "disabled"' in can1, "CAN1 must remain disabled in Z-011")
    require('status = "disabled"' in spi6, "SPI6 must remain disabled in Z-011")
    require("&adc1_in3_pa3" in adc1, "generated ADC1 pin is not PA3")
    require("&adc2_in10_pc0" in adc2, "generated ADC2 pin is not PC0")

    require("CONFIG_ADC=y" in cfg, "CONFIG_ADC must be enabled")
    require("CONFIG_ADC_ASYNC=y" in cfg,
            "CONFIG_ADC_ASYNC required for bounded 5 ms transaction")
    require("CONFIG_ADC_STM32_DMA=y" not in cfg,
            "DMA must remain disabled during current migration")
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in cfg,
            "Z-011 must remain no-BMS-authority")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in cfg,
            "Z-011 must remain no-balance-authority")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in cfg,
            "Z-011 must remain application-heap-free")

    for symbol in (
        "ams_current_adc_init",
        "ams_current_adc_read_pair",
        "ams_current_adc_is_faulted",
    ):
        require(symbol in link_map, f"linked current ADC symbol missing: {symbol}")

    print("PASS: Z-011 bounded Zephyr current-ADC adapter contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
