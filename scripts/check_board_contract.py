#!/usr/bin/env python3

import argparse
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

    user = get_block(text, "zephyr,user")
    require_contains(
        user,
        "bms-ok-gpios = < &gpioe 0x0 0x0 >",
        "BMS_OK PE0"
    )
    require_contains(
        user,
        "adbms-cs-a-gpios = < &gpioe 0x2 0x1 >",
        "ADBMS CS_A PE2"
    )
    require_contains(
        user,
        "adbms-cs-b-gpios = < &gpioe 0x4 0x1 >",
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
    require_contains(can, 'status = "disabled"', "CAN1 Z-005 state")

    spi = get_block(text, "spi6:")
    require_contains(
        spi,
        "pinctrl-0 = < &spi6_sck_pg13 &spi6_miso_pg12 &spi6_mosi_pg14 >",
        "SPI6 PG13/PG12/PG14"
    )
    require_contains(spi, 'status = "disabled"', "SPI6 Z-005 state")

    adc1 = get_block(text, "adc1:")
    require_contains(
        adc1,
        "pinctrl-0 = < &adc1_in3_pa3 >",
        "ADC1 high-range PA3"
    )
    require_contains(adc1, 'status = "disabled"', "ADC1 Z-005 state")

    adc2 = get_block(text, "adc2:")
    require_contains(
        adc2,
        "pinctrl-0 = < &adc2_in10_pc0 >",
        "ADC2 low-range PC0"
    )
    require_contains(adc2, 'status = "disabled"', "ADC2 Z-005 state")

    print("PASS: DER26 board contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())