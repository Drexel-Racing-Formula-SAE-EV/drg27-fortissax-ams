#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    header_path = (
        repo / "lib" / "ams_core" / "include" / "ams_core" /
        "ams_current_window.h"
    )
    source_path = (
        repo / "lib" / "ams_core" / "measurement" /
        "ams_current_window.c"
    )
    cmake_path = repo / "lib" / "ams_core" / "CMakeLists.txt"
    config_path = build / "zephyr" / ".config"
    map_path = build / "zephyr" / "zephyr.map"

    for path in (header_path, source_path, cmake_path, config_path, map_path):
        require(path.is_file(), f"missing {path}")

    header = header_path.read_text(encoding="utf-8")
    source = source_path.read_text(encoding="utf-8")
    cmake = cmake_path.read_text(encoding="utf-8")
    config = config_path.read_text(encoding="utf-8", errors="replace")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")

    for name, expected in (
        ("AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS", 100),
        ("AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS", 100),
    ):
        match = re.search(
            rf"^#define\s+{name}\s+([0-9]+)U$",
            header,
            flags=re.MULTILINE,
        )
        require(match is not None, f"missing {name}")
        require(int(match.group(1)) == expected,
                f"{name} must remain {expected} ms")

    required_state = (
        "last_uncertainty_mA",
        "last_selected_range",
        "sample_uncertainty_mA",
        "sample_selected_range",
        "active_calibration_provenance_initialized",
        "active_sensor_metadata_initialized",
        "active_current_squared_A2s",
        "total_invalid_sample_count",
    )

    for token in required_state:
        require(token in header, f"current-window state missing: {token}")

    forbidden_tokens = (
        "zephyr/",
        "FreeRTOS",
        "taskENTER_CRITICAL",
        "taskEXIT_CRITICAL",
        "k_mutex",
        "k_spin",
        "malloc(",
        "calloc(",
        "realloc(",
        "free(",
        "HAL_",
    )

    for token in forbidden_tokens:
        require(token not in source,
                f"portable current-window core contains forbidden token: {token}")

    # Exact ordering/race contract from v2.6.27.
    require(
        "(uint32_t)(now - acc->active.end_tick) > INT32_MAX" in source,
        "late sample timestamp rejection missing",
    )
    require(
        "(uint32_t)(boundary_tick - acc->active.end_tick) > INT32_MAX" in source,
        "old voltage-boundary rejection missing",
    )
    require(
        "current_record_invalid(acc);\n        *completed = acc->active;" in source,
        "old boundary must taint and return active state without rotation",
    )
    require(
        "(uint32_t)(now - acc->last_sample_tick) <=\n            AMS_CURRENT_WINDOW_MAX_INTEGRATION_GAP_MS" in source,
        "real-sample gap check missing",
    )
    require(
        "acc->last_sample_valid = false;" in source,
        "stale tail must be invalidated before next epoch",
    )

    # Metadata carry contract.
    require(
        "acc->last_uncertainty_mA = acc->sample_uncertainty_mA;" in source,
        "accepted sample uncertainty carry missing",
    )
    require(
        "acc->last_selected_range = acc->sample_selected_range;" in source,
        "accepted sample range carry missing",
    )
    require(
        "ams_current_window_set_sensor_metadata(acc," in source and
        "acc->last_uncertainty_mA" in source and
        "acc->last_selected_range" in source,
        "next-window sensor metadata carry missing",
    )
    require(
        "active_sensor_metadata_initialized" in source and
        "acc->active.selected_range = 0U;" in source,
        "sticky mixed-range latch missing",
    )
    require(
        "current_merge_calibration_provenance(" in source and
        "acc->last_calibration_record_confident" in source,
        "calibration provenance carry missing",
    )
    require(
        "AMS_CURRENT_UNCERTAINTY_UNKNOWN" in source,
        "unknown uncertainty initialization missing",
    )

    require(
        "measurement/ams_current_window.c" in cmake,
        "current-window source is not part of ams_core",
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set" in config,
        "Z-008 must remain no-authority",
    )
    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set" in config,
        "Z-008 must remain no-balance-authority",
    )
    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in config,
        "Z-008 must remain application-heap-free",
    )

    for symbol in (
        "ams_current_window_init",
        "ams_current_window_update",
        "ams_current_window_set_sensor_metadata",
        "ams_current_window_rotate",
    ):
        require(symbol in link_map,
                f"linked current-window symbol missing: {symbol}")

    print("PASS: v2.6.27 current-window contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
