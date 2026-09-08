#!/usr/bin/env python3
"""Target/build contract for the Z-015 privately owned bounded SPI6 substrate."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def symbol_enabled(config: str, symbol: str) -> bool:
    return re.search(rf"^{re.escape(symbol)}=y$", config, re.MULTILINE) is not None


def symbol_disabled(config: str, symbol: str) -> bool:
    return not symbol_enabled(config, symbol)


def dts_block(text: str, label: str) -> str:
    # Generated DTS labels are source-line anchored. Avoid phandle references.
    match = re.search(rf"(?m)^\s*{re.escape(label)}[^\n]*\{{", text)
    require(match is not None, f"missing generated DTS block: {label}")
    start = match.start()
    brace = text.find("{", match.start())
    depth = 0
    for idx in range(brace, len(text)):
        if text[idx] == "{":
            depth += 1
        elif text[idx] == "}":
            depth -= 1
            if depth == 0:
                return text[start:idx + 1]
    fail(f"unterminated generated DTS block: {label}")


def prop_int(block: str, name: str) -> int:
    m = re.search(rf"(?m)^\s*{re.escape(name)}\s*=\s*<\s*(0x[0-9a-fA-F]+|[0-9]+)\s*>\s*;", block)
    require(m is not None, f"missing scalar DTS property {name}")
    return int(m.group(1), 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    config_p = build / "zephyr/.config"
    dts_p = build / "zephyr/zephyr.dts"
    map_p = build / "zephyr/zephyr.map"
    target_p = repo / "drivers/ams/adbms_spi_stm32.c"
    engine_p = repo / "drivers/ams/adbms_spi_engine.c"
    lifecycle_p = repo / "include/ams_platform/adbms_spi_lifecycle.h"
    internal_p = repo / "drivers/ams/adbms_spi_internal.h"

    for p in (config_p, dts_p, map_p, target_p, engine_p, lifecycle_p, internal_p):
        require(p.is_file(), f"missing Z-015 target-contract artifact: {p}")

    cfg = config_p.read_text(encoding="utf-8", errors="replace")
    dts = dts_p.read_text(encoding="utf-8", errors="replace")
    link_map = map_p.read_text(encoding="utf-8", errors="replace")
    target = target_p.read_text(encoding="utf-8")

    for sym in (
        "CONFIG_AMS_ADBMS_SPI_PRIVATE_BACKEND",
        "CONFIG_USE_STM32_LL_SPI",
        "CONFIG_RESET",
        "CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT",
    ):
        require(symbol_enabled(cfg, sym), f"required Z-015 target symbol disabled: {sym}")

    for sym in (
        "CONFIG_SPI",
        "CONFIG_SPI_ASYNC",
        "CONFIG_SPI_RTIO",
        "CONFIG_SPI_STM32_DMA",
        "CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED",
        "CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE",
        "CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE",
        "CONFIG_AMS_BMS_AUTHORITY",
        "CONFIG_AMS_BALANCE_AUTHORITY",
    ):
        require(symbol_disabled(cfg, sym), f"Z-015 target symbol must remain disabled: {sym}")

    require(symbol_enabled(cfg, "CONFIG_ARCH_HAS_IRQ_PENDING_OPS"),
            "private SPI6 recovery requires IRQ pending-clear support")

    adbms = dts_block(dts, "ams_adbms_interface:")
    spi6 = dts_block(dts, "spi6:")
    rcc = dts_block(dts, "rcc:")

    require('compatible = "drexel,ams-adbms-interface"' in adbms,
            "generated typed ADBMS interface missing")
    require('status = "okay"' in adbms, "typed ADBMS interface disabled")
    require('status = "disabled"' in spi6,
            "stock spi_stm32 SPI6 device must remain disabled")
    require("pinctrl-0" not in spi6 and "cs-gpios" not in spi6 and "st,soft-nss" not in spi6,
            "stock SPI6 node owns pins/CS/NSS despite private backend")

    for pin in ("spi6_sck_pg13", "spi6_miso_pg12", "spi6_mosi_pg14"):
        require(pin in adbms, f"private ADBMS pinctrl missing {pin}")
    require("gpioe 0x2 0x1" in adbms or "&gpioe 2" in adbms,
            "generated CS_A is not PE2 active-low")
    require("gpioe 0x4 0x1" in adbms or "&gpioe 4" in adbms,
            "generated CS_B is not PE4 active-low")

    expected = {
        "spi-input-clock-hz": 108_000_000,
        "spi-prescaler": 256,
        "spi-frequency-hz": 421_875,
        "spi-timeout-ms": 500,
        "max-transfer-bytes": 512,
        "read-dummy-byte": 255,
    }
    for name, value in expected.items():
        require(prop_int(adbms, name) == value,
                f"generated ADBMS transport property drift: {name}")

    require(prop_int(rcc, "clock-frequency") == 216_000_000,
            "generated SYSCLK is not 216 MHz")
    require(prop_int(rcc, "apb2-prescaler") == 2,
            "generated APB2 prescaler is not /2")
    pclk2 = prop_int(rcc, "clock-frequency") // prop_int(rcc, "apb2-prescaler")
    require(pclk2 == expected["spi-input-clock-hz"],
            "generated clock tree does not produce 108 MHz SPI6 input")
    achieved = pclk2 // expected["spi-prescaler"]
    require(achieved == expected["spi-frequency-hz"] == 421_875,
            "generated clock/prescaler does not achieve exactly 421875 Hz")

    # The lifecycle implementation must be linked because startup calls it.
    for sym in ("ams_adbms_spi_platform_init", "ams_adbms_spi_platform_status"):
        require(sym in link_map, f"linked Z-015 lifecycle symbol missing: {sym}")

    # The generic STM32 SPI transaction driver must not exist in this image.
    for forbidden in ("spi_stm32.c.obj", "spi_stm32_isr", "spi_stm32_complete",
                      "spi_transceive_signal", "spi_transceive_cb"):
        require(forbidden not in link_map,
                f"stock/async STM32 SPI path linked into private Z-015 image: {forbidden}")

    for token in (
        "LL_SPI_POLARITY_HIGH", "LL_SPI_PHASE_2EDGE", "LL_SPI_DATAWIDTH_8BIT",
        "LL_SPI_MSB_FIRST", "LL_SPI_FULL_DUPLEX", "LL_SPI_NSS_SOFT",
        "LL_SPI_BAUDRATEPRESCALER_DIV256", "clock_control_get_rate",
        "reset_line_toggle_dt", "irq_disable(", "k_irq_clear_pending(",
    ):
        require(token in target, f"production private SPI6 contract missing: {token}")
    for forbidden in ("spi_transceive", "k_poll_signal", "IRQ_CONNECT", "irq_enable(",
                      "k_sleep(", "k_yield(", "irq_lock(", "k_sched_lock("):
        require(forbidden not in target, f"forbidden Z-015 transport mechanism present: {forbidden}")

    print("PASS: Z-015 private bounded SPI6 target/build contract")
    print("  generic spi_stm32: absent; SPI6 DT device disabled")
    print("  clock chain: 216MHz SYSCLK -> APB2/2 -> 108MHz -> /256 -> 421875Hz")
    print("  IRQ/DMA/async: absent; private lifecycle linked; runtime transfer callers absent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
