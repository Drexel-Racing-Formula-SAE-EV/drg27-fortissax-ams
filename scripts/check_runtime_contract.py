#!/usr/bin/env python3

import argparse
import re
from pathlib import Path
import sys


EXPECTED_MACROS = {
    # Zephyr priority numbers differ from FreeRTOS; ordering is checked below.
    "AMS_PRIO_SAFETY": 0,
    "AMS_PRIO_CURRENT": 2,
    "AMS_PRIO_ADBMS": 3,
    "AMS_PRIO_CAN": 4,
    "AMS_PRIO_ESTIMATOR": 6,
    "AMS_PRIO_FAN": 8,
    "AMS_PRIO_AIR": 8,
    "AMS_PRIO_IMD": 9,
    "AMS_PRIO_DIAGNOSTICS": 12,

    # Exact normal-rate v2.6.27 task periods.
    "AMS_PERIOD_SAFETY_MS": 50,
    "AMS_PERIOD_CURRENT_MS": 20,
    "AMS_PERIOD_ADBMS_MS": 100,
    "AMS_PERIOD_CAN_MS": 100,
    "AMS_PERIOD_ESTIMATOR_MS": 100,
    "AMS_PERIOD_FAN_MS": 200,
    "AMS_PERIOD_AIR_MS": 500,
    "AMS_PERIOD_IMD_MS": 100,
    "AMS_PERIOD_DIAGNOSTICS_MS": 0,

    # Exact v2.6.27 heartbeat policy rather than a generic N-period rule.
    "AMS_HEARTBEAT_STARTUP_GRACE_MS": 3000,
    "AMS_HEARTBEAT_ADBMS_TIMEOUT_MS": 3000,
    "AMS_HEARTBEAT_CURRENT_TIMEOUT_MS": 200,
    "AMS_HEARTBEAT_TEMP_TIMEOUT_MS": 3000,
    "AMS_HEARTBEAT_CAN_TIMEOUT_MS": 2000,
    "AMS_HEARTBEAT_LOGGER_TIMEOUT_MS": 2000,
    "AMS_HEARTBEAT_IMD_TIMEOUT_MS": 500,
    "AMS_HEARTBEAT_FAN_TIMEOUT_MS": 1000,
    "AMS_HEARTBEAT_ESTIMATOR_TIMEOUT_MS": 500,

    "AMS_STALE_SAFETY_MS": 0,
    "AMS_STALE_CURRENT_MS": 200,
    "AMS_STALE_ADBMS_MS": 3000,
    "AMS_STALE_CAN_MS": 2000,
    "AMS_STALE_ESTIMATOR_MS": 500,
    "AMS_STALE_FAN_MS": 1000,
    "AMS_STALE_AIR_MS": 0,
    "AMS_STALE_IMD_MS": 500,
    "AMS_STALE_DIAGNOSTICS_MS": 0,

    "AMS_STARTUP_SAFETY_MS": 0,
    "AMS_STARTUP_CURRENT_MS": 3000,
    "AMS_STARTUP_ADBMS_MS": 3000,
    "AMS_STARTUP_CAN_MS": 3000,
    "AMS_STARTUP_ESTIMATOR_MS": 3000,
    "AMS_STARTUP_FAN_MS": 3000,
    "AMS_STARTUP_AIR_MS": 0,
    "AMS_STARTUP_IMD_MS": 3000,
    "AMS_STARTUP_DIAGNOSTICS_MS": 0,

    # Frozen bounded waits from the FreeRTOS oracle for later adapters.
    "AMS_ADBMS_MUTEX_TIMEOUT_MS": 500,
    "AMS_CURRENT_WINDOW_MUTEX_TIMEOUT_MS": 20,

    # Current migration profile has no validated AIR/IMD adapter authority.
    "AMS_RUNTIME_AIR_ENABLED": 0,
    "AMS_RUNTIME_IMD_ENABLED": 0,
    "AMS_RUNTIME_ESTIMATOR_SAFETY_REQUIRED": 0,

    # Conservative Zephyr allocations.
    "AMS_STACK_SAFETY": 2048,
    "AMS_STACK_CURRENT": 2048,
    "AMS_STACK_ADBMS": 8192,
    "AMS_STACK_CAN": 8192,
    "AMS_STACK_ESTIMATOR": 8192,
    "AMS_STACK_FAN": 1536,
    "AMS_STACK_AIR": 1536,
    "AMS_STACK_IMD": 1536,
    "AMS_STACK_DIAGNOSTICS": 4096,

    # Exact v2.6.27 byte-equivalent lower bounds.
    "AMS_ORACLE_STACK_SAFETY_BYTES": 1024,
    "AMS_ORACLE_STACK_CURRENT_BYTES": 1024,
    "AMS_ORACLE_STACK_ADBMS_BYTES": 6144,
    "AMS_ORACLE_STACK_CAN_BYTES": 6144,
    "AMS_ORACLE_STACK_ESTIMATOR_BYTES": 6144,
    "AMS_ORACLE_STACK_FAN_BYTES": 768,
    "AMS_ORACLE_STACK_AIR_BYTES": 768,
    "AMS_ORACLE_STACK_IMD_BYTES": 768,
    "AMS_ORACLE_STACK_DIAGNOSTICS_BYTES": 2048,
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
    header_path = args.repo_root / "app" / "src" / "ams_threads.h"
    map_path = args.build_dir / "zephyr" / "zephyr.map"
    config_path = args.build_dir / "zephyr" / ".config"

    require(source_path.is_file(), f"missing {source_path}")
    require(header_path.is_file(), f"missing {header_path}")
    require(map_path.is_file(), f"missing {map_path}")
    require(config_path.is_file(), f"missing {config_path}")

    source = source_path.read_text(encoding="utf-8")
    header = header_path.read_text(encoding="utf-8")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")
    config = config_path.read_text(encoding="utf-8")

    values = {}
    for macro, expected in EXPECTED_MACROS.items():
        actual = parse_macro(source, macro)
        values[macro] = actual
        require(actual == expected, f"{macro}: expected {expected}, got {actual}")

    # Preserve the exact v2.6.27 relative priority policy after translating
    # larger-is-higher FreeRTOS priorities to smaller-is-higher Zephyr values.
    require(values["AMS_PRIO_SAFETY"] < values["AMS_PRIO_CURRENT"],
            "safety must outrank current")
    require(values["AMS_PRIO_CURRENT"] < values["AMS_PRIO_ADBMS"],
            "current must outrank ADBMS")
    require(values["AMS_PRIO_ADBMS"] < values["AMS_PRIO_CAN"],
            "ADBMS must outrank CAN")
    require(values["AMS_PRIO_CAN"] < values["AMS_PRIO_ESTIMATOR"],
            "CAN must outrank estimator")
    require(values["AMS_PRIO_ESTIMATOR"] < values["AMS_PRIO_FAN"],
            "estimator must outrank fan")
    require(values["AMS_PRIO_FAN"] == values["AMS_PRIO_AIR"],
            "fan and AIR priorities must match")
    require(values["AMS_PRIO_FAN"] < values["AMS_PRIO_IMD"],
            "fan/AIR must outrank IMD")
    require(values["AMS_PRIO_IMD"] < values["AMS_PRIO_DIAGNOSTICS"],
            "IMD must outrank diagnostics")

    # Zephyr stacks may be larger, never smaller, before target watermark data.
    stack_pairs = (
        ("AMS_STACK_SAFETY", "AMS_ORACLE_STACK_SAFETY_BYTES"),
        ("AMS_STACK_CURRENT", "AMS_ORACLE_STACK_CURRENT_BYTES"),
        ("AMS_STACK_ADBMS", "AMS_ORACLE_STACK_ADBMS_BYTES"),
        ("AMS_STACK_CAN", "AMS_ORACLE_STACK_CAN_BYTES"),
        ("AMS_STACK_ESTIMATOR", "AMS_ORACLE_STACK_ESTIMATOR_BYTES"),
        ("AMS_STACK_FAN", "AMS_ORACLE_STACK_FAN_BYTES"),
        ("AMS_STACK_AIR", "AMS_ORACLE_STACK_AIR_BYTES"),
        ("AMS_STACK_IMD", "AMS_ORACLE_STACK_IMD_BYTES"),
        ("AMS_STACK_DIAGNOSTICS", "AMS_ORACLE_STACK_DIAGNOSTICS_BYTES"),
    )
    for actual_name, oracle_name in stack_pairs:
        require(values[actual_name] >= values[oracle_name],
                f"{actual_name} below v2.6.27 lower bound")

    for symbol in REQUIRED_STACK_SYMBOLS:
        require(symbol in link_map, f"missing explicit stack symbol: {symbol}")

    require("air_thread" in link_map, "AIR thread object missing")
    require("imd_thread" in link_map, "IMD thread object missing")

    require("CONFIG_THREAD_STACK_INFO=y" in config,
            "thread stack info must be enabled")
    require("CONFIG_INIT_STACKS=y" in config,
            "stack initialization must be enabled")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in config,
            "runtime skeleton must not require heap")

    forbidden_workqueue_calls = (
        "k_work_submit",
        "k_work_schedule",
        "k_work_reschedule",
        "k_sys_work_q",
    )
    for token in forbidden_workqueue_calls:
        require(token not in source,
                f"critical AMS runtime must not use system workqueue: {token}")

    require("K_TIMEOUT_ABS_MS" in source,
            "periodic AMS threads must use absolute release timing")
    require("k_cycle_get_32" in source,
            "execution timing instrumentation missing")
    require("k_thread_stack_space_get" in source,
            "stack watermark instrumentation missing")

    # Disabled placeholders must not be used as fabricated liveness proof.
    require("if (!thread->enabled || (thread->stale_deadline_ms == 0U))" in source,
            "disabled/non-heartbeat threads are not excluded from stale logic")
    require("start_thread_if_enabled(AMS_THREAD_AIR);" in source,
            "AIR start must remain profile/adapter gated")
    require("start_thread_if_enabled(AMS_THREAD_IMD);" in source,
            "IMD start must remain profile/adapter gated")
    require("bool enabled;" in header,
            "runtime snapshot must expose enabled state")
    require("bool safety_heartbeat_required;" in header,
            "runtime snapshot must expose safety-heartbeat membership")
    require("bool safety_evidence_ready;" in header,
            "runtime snapshot must expose safety-evidence readiness")
    require(".safety_evidence_ready = true" not in source,
            "Z-010 placeholder loop must not count as safety liveness evidence")

    # Supervisor must be activated before lower-priority work once the shared
    # runtime epoch and startup grace are established.
    safety_start = source.find("start_thread_if_enabled(AMS_THREAD_SAFETY);")
    current_start = source.find("start_thread_if_enabled(AMS_THREAD_CURRENT);")
    require((safety_start >= 0) and (current_start > safety_start),
            "safety supervisor must start before worker threads")

    print("PASS: AMS runtime contract (v2.6.27 safety-policy parity)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
