#!/usr/bin/env python3

import argparse
import re
from pathlib import Path
import sys


EXPECTED_MACROS = {
    "AMS_PRIO_SAFETY": 0,
    "AMS_PRIO_CURRENT": 2,
    "AMS_PRIO_ADBMS": 3,
    "AMS_PRIO_CAN": 4,
    "AMS_PRIO_ESTIMATOR": 6,
    "AMS_PRIO_FAN": 8,
    "AMS_PRIO_AIR": 8,
    "AMS_PRIO_IMD": 9,
    "AMS_PRIO_DIAGNOSTICS": 12,

    "AMS_PERIOD_SAFETY_MS": 50,
    "AMS_PERIOD_CURRENT_MS": 20,
    "AMS_PERIOD_ADBMS_MS": 100,
    "AMS_PERIOD_CAN_MS": 100,
    "AMS_PERIOD_ESTIMATOR_MS": 100,
    "AMS_PERIOD_FAN_MS": 200,
    "AMS_PERIOD_AIR_MS": 500,
    "AMS_PERIOD_IMD_MS": 100,
    "AMS_PERIOD_DIAGNOSTICS_MS": 0,

    "AMS_STALE_SAFETY_MS": 150,
    "AMS_STALE_CURRENT_MS": 60,
    "AMS_STALE_ADBMS_MS": 300,
    "AMS_STALE_CAN_MS": 300,
    "AMS_STALE_ESTIMATOR_MS": 300,
    "AMS_STALE_FAN_MS": 600,
    "AMS_STALE_AIR_MS": 1500,
    "AMS_STALE_IMD_MS": 300,
    "AMS_STALE_DIAGNOSTICS_MS": 0,

    "AMS_STACK_SAFETY": 2048,
    "AMS_STACK_CURRENT": 2048,
    "AMS_STACK_ADBMS": 8192,
    "AMS_STACK_CAN": 8192,
    "AMS_STACK_ESTIMATOR": 8192,
    "AMS_STACK_FAN": 1536,
    "AMS_STACK_AIR": 1536,
    "AMS_STACK_IMD": 1536,
    "AMS_STACK_DIAGNOSTICS": 4096,
}


REQUIRED_STACK_SYMBOLS = (
    "safety_stack",
    "current_stack",
    "adbms_stack",
    "can_stack",
    "estimator_stack",
    "fan_stack",
    "air_stack",
    "imd_stack",
    "diagnostics_stack",
)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def parse_macro(source: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)U?\s*$",
        source,
        flags=re.MULTILINE,
    )

    if not match:
        fail(f"missing runtime macro {name}")

    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    source_path = args.repo_root / "app" / "src" / "ams_threads.c"
    map_path = args.build_dir / "zephyr" / "zephyr.map"
    config_path = args.build_dir / "zephyr" / ".config"

    require(source_path.is_file(), f"missing {source_path}")
    require(map_path.is_file(), f"missing {map_path}")
    require(config_path.is_file(), f"missing {config_path}")

    source = source_path.read_text(encoding="utf-8")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")
    config = config_path.read_text(encoding="utf-8")

    for macro, expected in EXPECTED_MACROS.items():
        actual = parse_macro(source, macro)

        require(
            actual == expected,
            f"{macro}: expected {expected}, got {actual}"
        )

    for symbol in REQUIRED_STACK_SYMBOLS:
        require(
            symbol in link_map,
            f"missing explicit stack symbol: {symbol}"
        )

    require(
        "air_thread" in link_map,
        "AIR thread object missing"
    )

    require(
        "CONFIG_THREAD_STACK_INFO=y" in config,
        "thread stack info must be enabled"
    )

    require(
        "CONFIG_INIT_STACKS=y" in config,
        "stack initialization must be enabled"
    )

    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in config,
        "runtime skeleton must not require heap"
    )

    forbidden_workqueue_calls = (
        "k_work_submit",
        "k_work_schedule",
        "k_work_reschedule",
        "k_sys_work_q",
    )

    for token in forbidden_workqueue_calls:
        require(
            token not in source,
            f"critical AMS runtime must not use system workqueue: {token}"
        )

    require(
        "K_TIMEOUT_ABS_MS" in source,
        "periodic AMS threads must use absolute release timing"
    )

    require(
        "k_cycle_get_32" in source,
        "execution timing instrumentation missing"
    )

    require(
        "k_thread_stack_space_get" in source,
        "stack watermark instrumentation missing"
    )

    print("PASS: AMS runtime contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())