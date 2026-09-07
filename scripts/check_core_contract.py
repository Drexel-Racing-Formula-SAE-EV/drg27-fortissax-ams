#!/usr/bin/env python3

import argparse
import re
from pathlib import Path
import sys


EXPECTED_NUMERIC_MACROS = {
    "AMS_PHYSICAL_SEGMENT_COUNT": 5,
    "AMS_CELLS_PER_SEGMENT": 15,
    "AMS_TEMP_SENSORS_PER_SEGMENT": 24,
}


FORBIDDEN_PATTERNS = {
    "Zephyr include": re.compile(
        r'^\s*#\s*include\s*[<"]zephyr/',
        re.MULTILINE,
    ),
    "FreeRTOS": re.compile(r"\bFreeRTOS\b"),
    "CMSIS-RTOS": re.compile(r"\bcmsis_os[0-9_]*\b"),
    "STM32 HAL API": re.compile(r"\bHAL_[A-Za-z0-9_]+\b"),
    "STM32 include": re.compile(
        r'^\s*#\s*include\s*[<"]stm32',
        re.MULTILINE | re.IGNORECASE,
    ),
    "FreeRTOS task API": re.compile(
        r"\b(?:xTask|vTask|taskENTER_|taskEXIT_)[A-Za-z0-9_]*\b"
    ),
    "CMSIS thread/mutex API": re.compile(
        r"\b(?:osThread|osMutex)[A-Za-z0-9_]*\b"
    ),
    "Zephyr kernel API": re.compile(
        r"\b(?:k_mutex|k_sem|k_spin|k_sleep|k_msleep|k_work)[A-Za-z0-9_]*\b"
    ),
    "STM32 GPIO register": re.compile(r"\bGPIO[A-I]\b"),
    "STM32 RCC register": re.compile(r"\bRCC\s*->"),
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def parse_numeric_macro(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)[uU]?\s*$",
        text,
        flags=re.MULTILINE,
    )

    if not match:
        fail(f"missing or non-numeric macro {name}")

    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    core = repo / "lib" / "ams_core"
    config_header = (
        core
        / "include"
        / "ams_core"
        / "ams_core_config.h"
    )
    map_path = build / "zephyr" / "zephyr.map"
    dot_config = build / "zephyr" / ".config"

    require(core.is_dir(), f"missing portable core: {core}")
    require(config_header.is_file(), f"missing {config_header}")
    require(map_path.is_file(), f"missing {map_path}")
    require(dot_config.is_file(), f"missing {dot_config}")

    config_text = config_header.read_text(encoding="utf-8")

    for macro, expected in EXPECTED_NUMERIC_MACROS.items():
        actual = parse_numeric_macro(config_text, macro)

        require(
            actual == expected,
            f"{macro}: expected {expected}, got {actual}",
        )

    require(
        re.search(
            r"^\s*#define\s+AMS_CURRENT_UNCERTAINTY_UNKNOWN\s+UINT16_MAX\s*$",
            config_text,
            flags=re.MULTILINE,
        )
        is not None,
        "unknown current uncertainty must remain UINT16_MAX",
    )

    source_files = sorted(
        path
        for path in core.rglob("*")
        if path.suffix.lower() in {".c", ".h"}
    )

    require(source_files, "portable core has no C/header sources")

    for path in source_files:
        text = path.read_text(
            encoding="utf-8",
            errors="replace",
        )

        for description, pattern in FORBIDDEN_PATTERNS.items():
            match = pattern.search(text)

            if match:
                relative = path.relative_to(repo)

                fail(
                    f"{description} dependency in "
                    f"{relative}: '{match.group(0)}'"
                )

    link_map = map_path.read_text(
        encoding="utf-8",
        errors="replace",
    )

    required_symbols = (
        "ams_core_contract_check",
        "ams_elapsed_ms",
        "ams_age_within_ms",
        "ams_age_expired_ms",
    )

    for symbol in required_symbols:
        require(
            symbol in link_map,
            f"portable-core symbol not linked: {symbol}",
        )

    build_config = dot_config.read_text(
        encoding="utf-8",
        errors="replace",
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set"
        in build_config,
        "Z-006 must remain no-authority",
    )

    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set"
        in build_config,
        "Z-006 must remain no-balance-authority",
    )

    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0"
        in build_config,
        "Z-006 must remain heap-free",
    )

    print("PASS: portable AMS core contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())