#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys


FORBIDDEN_SYMBOLS = (
    "ams_bms_ok_assert",
    "ams_bms_ok_set_high",
    "ams_bms_ok_enable",
    "ams_balance_enable_authority",
)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    config_path = args.build_dir / "zephyr" / ".config"
    map_path = args.build_dir / "zephyr" / "zephyr.map"

    require(config_path.is_file(), f"missing {config_path}")
    require(map_path.is_file(), f"missing {map_path}")

    config = config_path.read_text(encoding="utf-8")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")

    require(
        'CONFIG_BOARD="der26_ams"' in config,
        "build is not for der26_ams"
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set" in config,
        "BMS authority must remain disabled"
    )

    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set" in config,
        "balance authority must remain disabled"
    )

    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in config,
        "application heap must remain disabled"
    )

    require(
        "ams_bms_ok_force_low_direct" in link_map,
        "direct fail-low symbol missing"
    )

    require(
        "k_sys_fatal_error_handler" in link_map,
        "fatal safety hook missing"
    )

    for symbol in FORBIDDEN_SYMBOLS:
        require(
            symbol not in link_map,
            f"forbidden authority symbol linked: {symbol}"
        )

    print("PASS: no-authority build contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())