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


def get_block(text: str, label: str) -> str:
    start = text.find(label)

    if start < 0:
        fail(f"missing devicetree block: {label}")

    brace = text.find("{", start)

    if brace < 0:
        fail(f"malformed devicetree block: {label}")

    depth = 0

    for index in range(brace, len(text)):
        char = text[index]

        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1

            if depth == 0:
                return text[start:index + 1]

    fail(f"unterminated devicetree block: {label}")


def require_contains(block: str, value: str, description: str) -> None:
    require(value in block, f"{description}: expected '{value}'")


def string_list_property(block: str, name: str) -> list[str]:
    # Zephyr's generated zephyr.dts is semantically stable but its pretty-
    # printing may place string-list entries on separate lines. Parse the
    # property instead of coupling a safety contract to whitespace/layout.
    match = re.search(
        rf"(?:^|\n)\s*{re.escape(name)}\s*=\s*((?:\"[^\"]*\"\s*,?\s*)+);",
        block,
        flags=re.MULTILINE,
    )
    require(match is not None, f"missing devicetree string-list property: {name}")
    return re.findall(r'\"([^\"]*)\"', match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    dts_path = args.build_dir / "zephyr" / "zephyr.dts"

    require(dts_path.is_file(), f"missing {dts_path}")

    text = dts_path.read_text(encoding="utf-8")

    require(
        'model = "Drexel Racing DER26 AMS";' in text,
        "wrong board model"
    )

    chosen = get_block(text, "chosen")
    require_contains(chosen, "zephyr,sram = &sram0", "chosen SRAM")
    require_contains(chosen, "zephyr,dtcm = &dtcm", "chosen DTCM")
    require_contains(chosen, "zephyr,console = &usart3", "console")

    sram = get_block(text, "sram0:")
    require_contains(
        sram,
        "reg = < 0x20020000 0x60000 >",
        "384 KiB SRAM0 region"
    )

    dtcm = get_block(text, "dtcm:")
    require_contains(
        dtcm,
        "reg = < 0x20000000 0x20000 >",
        "128 KiB DTCM region"
    )

    hse = get_block(text, "clk_hse:")
    require_contains(hse, "clock-frequency = < 0x7a1200 >", "8 MHz HSE")
    require_contains(hse, 'status = "okay"', "HSE enabled")
    require(
        "hse-bypass" not in hse,
        "DER26 requires crystal HSE, not bypass mode"
    )

    pll = get_block(text, "pll:")
    require_contains(pll, "div-m = < 0x4 >", "PLLM")
    require_contains(pll, "mul-n = < 0xd8 >", "PLLN")
    require_contains(pll, "div-p = < 0x2 >", "PLLP")
    require_contains(pll, "div-q = < 0x2 >", "PLLQ")
    require_contains(pll, "clocks = < &clk_hse >", "PLL source")

    rcc = get_block(text, "rcc:")
    require_contains(
        rcc,
        "clock-frequency = < 0xcdfe600 >",
        "216 MHz SYSCLK"
    )
    require_contains(rcc, "ahb-prescaler = < 0x1 >", "AHB prescaler")
    require_contains(rcc, "apb1-prescaler = < 0x4 >", "APB1 prescaler")
    require_contains(rcc, "apb2-prescaler = < 0x2 >", "APB2 prescaler")

    safety_io = get_block(text, "ams_safety_io:")
    require_contains(safety_io, 'compatible = "drexel,ams-safety-io"',
                     "typed BMS safety node")
    require_contains(
        safety_io,
        "bms-ok-gpios = < &gpioe 0x0 0x0 >",
        "BMS_OK PE0"
    )

    adbms_if = get_block(text, "ams_adbms_interface:")
    require_contains(adbms_if, 'compatible = "drexel,ams-adbms-interface"',
                     "typed ADBMS interface node")
    require_contains(
        adbms_if,
        "cs-a-gpios = < &gpioe 0x2 0x1 >",
        "ADBMS CS_A PE2"
    )
    require_contains(
        adbms_if,
        "cs-b-gpios = < &gpioe 0x4 0x1 >",
        "ADBMS CS_B PE4"
    )

    usart = get_block(text, "usart3:")
    require_contains(
        usart,
        "pinctrl-0 = < &usart3_tx_pd8 &usart3_rx_pd9 >",
        "USART3 PD8/PD9"
    )
    require_contains(usart, 'status = "okay"', "USART3 enabled")

    can = get_block(text, "can1:")
    require_contains(can, '"st,stm32-bxcan"', "CAN1 must be bxCAN")
    require_contains(
        can,
        "pinctrl-0 = < &can1_rx_pd0 &can1_tx_pd1 >",
        "CAN1 PD0/PD1"
    )
    require_contains(can, 'status = "disabled"', "CAN1 Z-011 state")

    # Z-015 private ownership: the stock SPI6 node stays disabled while the
    # typed AMS interface owns the exact pinctrl contract.
    require_contains(
        adbms_if,
        "pinctrl-0 = < &spi6_sck_pg13 &spi6_miso_pg12 &spi6_mosi_pg14 >",
        "private ADBMS SPI6 PG13/PG12/PG14"
    )
    spi = get_block(text, "spi6:")
    require_contains(spi, 'status = "disabled"', "stock SPI6 remains disabled")

    # Post-Z-015 HAL audit hardening: current ADC1/ADC2 are privately owned by
    # the bounded polling adapter. Generic adc_stm32 devices and their shared
    # IRQ/context remain disabled; the typed AMS node owns pinctrl/reset and
    # exact acquisition parameters. ADC3 is also frozen disabled because F767
    # exposes one common ADCRST bit for ADC1/2/3.
    current = get_block(text, "ams_current_sense:")
    require_contains(current, 'compatible = "drexel,ams-current-sense"',
                     "typed current-sense node")
    require_contains(current,
                     "pinctrl-0 = < &adc1_in3_pa3 &adc2_in10_pc0 >",
                     "private current ADC PA3/PC0 pinctrl")
    require_contains(current, "high-channel = < 0x3 >", "current high channel")
    require_contains(current, "low-channel = < 0xa >", "current low channel")
    require_contains(current, "adc-prescaler = < 0x6 >", "current ADC /6 prescaler")
    require_contains(current, "conversion-timeout-ms = < 0x5 >", "current ADC 5 ms timeout")

    adc1 = get_block(text, "adc1:")
    adc2 = get_block(text, "adc2:")
    adc3 = get_block(text, "adc3:")
    require_contains(adc1, 'status = "disabled"', "ADC1 private-owner state")
    require_contains(adc2, 'status = "disabled"', "ADC2 private-owner state")
    require_contains(adc3, 'status = "disabled"', "ADC3 common-reset exclusion state")

    # Z-012 fan PWM timers. Exact channel/pin/frequency semantics are checked
    # in the dedicated fan contract as well.
    for label, pins in (
        ("timers3:", "&tim3_ch2_pa7 &tim3_ch4_pb1"),
        ("timers4:", "&tim4_ch3_pd14 &tim4_ch4_pd15"),
        ("timers5:", "&tim5_ch1_pa0 &tim5_ch2_pa1"),
    ):
        timer = get_block(text, label)
        require_contains(timer, 'status = "okay"', f"{label} Z-012 enabled")
        require_contains(timer, "st,prescaler = < 0x0 >", f"{label} prescaler 0")
        require(pins in timer, f"{label} fan pin mapping drift")

    fan_bank = get_block(text, "ams_fans:")
    require_contains(fan_bank, 'compatible = "drexel,ams-fan-bank"',
                     "typed fan-bank node")
    require(
        string_list_property(fan_bank, "pwm-names")
        == ["fan1", "fan2", "fan3", "fan4", "fan5", "fan6"],
        "fan PWM names/order drift",
    )

    imd = get_block(text, "ams_imd:")
    require_contains(imd, 'compatible = "drexel,ams-imd"',
                     "typed IMD node")
    require_contains(imd, "status-gpios = < &gpioc 0x5 0x0 >",
                     "IMD OK_HS PC5")

    require("zephyr,user" not in text,
            "application hardware contracts must not regress to /zephyr,user")

    print("PASS: DER26 board contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())