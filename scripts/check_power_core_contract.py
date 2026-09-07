#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path
import sys


ORACLE_SHA256 = {
    "ams_soh.h":
        "20fe7e29da6531bceb159b38870ce3c4e0a2a0cb84205be2da3531ebf69be719",
    "ams_soh.c":
        "46c9b26444d69d09a82428707f636181a527d624c0734b4ce5aa240f7669a9d4",
    "ams_sop.h":
        "03a9c889d0b4291dacbd0a521033293e4c8f27442e4f5fd349442295d23b3773",
    "ams_sop.c":
        "ae4898ff09a6295b848a63bc15dd2071deadc722a11c7ec2a0399842f1ce7496",
    "ams_fuse_observer.h":
        "9f0dcb554860c418af0cb8014299f661e29d8dbb8b508069db73ea95a1c354ab",
    "ams_fuse_observer.c":
        "d0182a133c996487ef81166287ccbb05e2841ad496ece81d7c2bbdd867a163fa",
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def canonical_bytes(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")

    if path.name == "ams_soh.c":
        text = text.replace(
            "#include <ams_core/ams_soh.h>",
            '#include "soh/ams_soh.h"',
        )
    elif path.name == "ams_sop.c":
        text = text.replace(
            "#include <ams_core/ams_sop.h>",
            '#include "sop/ams_sop.h"',
        ).replace(
            "#include <ams_core/ams_estimator_lut.h>",
            '#include "estimator/ams_estimator_lut.h"',
        )
    elif path.name == "ams_fuse_observer.h":
        text = text.replace(
            "#include <ams_core/ams_sop.h>",
            '#include "sop/ams_sop.h"',
        )
    elif path.name == "ams_fuse_observer.c":
        text = text.replace(
            "#include <ams_core/ams_fuse_observer.h>",
            '#include "sop/ams_fuse_observer.h"',
        )

    return text.encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    core = repo / "lib" / "ams_core"
    include = core / "include" / "ams_core"

    files = {
        "ams_soh.h": include / "ams_soh.h",
        "ams_soh.c": core / "soh" / "ams_soh.c",
        "ams_sop.h": include / "ams_sop.h",
        "ams_sop.c": core / "sop" / "ams_sop.c",
        "ams_fuse_observer.h": include / "ams_fuse_observer.h",
        "ams_fuse_observer.c": core / "sop" / "ams_fuse_observer.c",
    }

    for path in files.values():
        require(path.is_file(), f"missing Z-010 power-core file: {path}")

    for name, path in files.items():
        digest = hashlib.sha256(canonical_bytes(path)).hexdigest()
        require(
            digest == ORACLE_SHA256[name],
            f"{name} diverged from frozen v2.6.27 oracle "
            f"(expected {ORACLE_SHA256[name]}, got {digest})",
        )

    sop_h = files["ams_sop.h"].read_text(encoding="utf-8")
    soh_h = files["ams_soh.h"].read_text(encoding="utf-8")
    fuse_h = files["ams_fuse_observer.h"].read_text(encoding="utf-8")
    sop_c = files["ams_sop.c"].read_text(encoding="utf-8")
    soh_c = files["ams_soh.c"].read_text(encoding="utf-8")
    fuse_c = files["ams_fuse_observer.c"].read_text(encoding="utf-8")

    for token in (
        "#define AMS_SOP_SEGMENTS             5u",
        "#define AMS_SOP_TOTAL_CELLS         75u",
        "#define AMS_SOP_HORIZONS             4u",
        "#define AMS_SOP_BISECTION_ITERS     16u",
        "AMS_SOP_REASON_ESTIMATOR_UNACQUIRED",
        "float max_temperature_age_ms;",
        "void ams_sop_apply_recovery(",
    ):
        require(token in sop_h, f"SoP contract token missing: {token}")

    for token in (
        "#define AMS_SOH_SEGMENTS 5u",
        "#define AMS_SOH_PERSIST_SCHEMA 3u",
        "#define AMS_SOH_RESISTANCE_EPISODE_MIN_OBSERVATIONS 9u",
        "#define AMS_SOH_RESISTANCE_EPISODE_MAX_OBSERVATIONS 33u",
        "#define AMS_SOH_RESISTANCE_EPISODE_GAP_MS 2500u",
        "uint32_t max_cell_age_ms;",
        "uint32_t max_temperature_age_ms;",
        "uint32_t ams_soh_record_crc32(",
    ):
        require(token in soh_h, f"SoH contract token missing: {token}")

    for token in (
        "#define AMS_FUSE_EAC14_80_RATED_CURRENT_A       80.0f",
        "float maximum_state_multiple;",
        "uint8_t initial_state_conservative;",
        "bool ams_fuse_observer_init_conservative(",
        "float ams_fuse_typical_melt_time_s(",
    ):
        require(token in fuse_h, f"fuse contract token missing: {token}")

    for token in (
        "AMS_SOP_REASON_MEASUREMENT_STALE",
        "AMS_SOP_REASON_CURRENT_UNCERTAINTY",
        "AMS_SOP_REASON_ESTIMATOR_UNACQUIRED",
        "AMS_SOP_BIND_MODEL_DOMAIN",
        "AMS_SOP_BIND_HORIZON_ENVELOPE",
    ):
        require(token in sop_c, f"SoP algorithm token missing: {token}")

    for token in (
        "AMS_SOH_REASON_SOC_UNCERTAINTY",
        "AMS_SOH_REASON_REST_INNOVATION",
        "AMS_SOH_REASON_REST_POLARIZATION",
        "segment_resistance_episode_ratio",
        "ams_soh_record_crc32",
    ):
        require(token in soh_c, f"SoH algorithm token missing: {token}")

    for token in (
        "AMS_FUSE_REASON_MODEL_UNVALIDATED",
        "AMS_FUSE_REASON_INITIAL_STATE_UNKNOWN",
        "AMS_FUSE_REASON_CURVE_EXTRAPOLATED",
        "maximum_state_multiple",
        "ams_fuse_typical_melt_time_s",
    ):
        require(token in fuse_c, f"fuse algorithm token missing: {token}")

    forbidden_patterns = {
        "Zephyr API": re.compile(r"\b(?:k_mutex|k_sem|k_spin|k_sleep|k_work)\b"),
        "Zephyr include": re.compile(r'^\s*#\s*include\s*[<"]zephyr/', re.MULTILINE),
        "FreeRTOS API": re.compile(r"\b(?:xTask|vTask|taskENTER_|taskEXIT_)[A-Za-z0-9_]*"),
        "FreeRTOS include": re.compile(r'^\s*#\s*include\s*[<"]FreeRTOS', re.MULTILINE),
        "CMSIS-RTOS": re.compile(r"\bcmsis_os[0-9_]*\b|\bosThread[A-Za-z0-9_]*\b"),
        "STM32 HAL": re.compile(r"\bHAL_[A-Za-z0-9_]+\b"),
        "dynamic allocation": re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\("),
    }

    for path in files.values():
        text = path.read_text(encoding="utf-8", errors="replace")
        for description, pattern in forbidden_patterns.items():
            match = pattern.search(text)
            require(
                match is None,
                f"{description} leaked into portable power core "
                f"{path.relative_to(repo)}: {match.group(0) if match else ''}",
            )

    # Scope boundary: these integration/CAN modules are intentionally not Z-010.
    for prohibited in (
        core / "sop" / "ams_power_state.c",
        core / "sop" / "ams_power_strategy.c",
        core / "sop" / "ams_power_can.c",
        include / "ams_power_state.h",
        include / "ams_power_strategy.h",
        include / "ams_power_can.h",
    ):
        require(
            not prohibited.exists(),
            f"Z-010 scope creep: integration/CAN file present: {prohibited}",
        )

    cmake = (core / "CMakeLists.txt").read_text(encoding="utf-8")
    for source in (
        "soh/ams_soh.c",
        "sop/ams_sop.c",
        "sop/ams_fuse_observer.c",
    ):
        require(source in cmake, f"power-core source not linked: {source}")

    dot_config = (build / "zephyr" / ".config").read_text(
        encoding="utf-8", errors="replace"
    )
    link_map = (build / "zephyr" / "zephyr.map").read_text(
        encoding="utf-8", errors="replace"
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set" in dot_config,
        "Z-010 must remain no-authority",
    )
    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set" in dot_config,
        "Z-010 must remain no-balance-authority",
    )
    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in dot_config,
        "Z-010 must remain application-heap-free",
    )

    for symbol in (
        "ams_sop_default_config",
        "ams_sop_solve",
        "ams_sop_apply_recovery",
        "ams_soh_default_config",
        "ams_soh_update",
        "ams_soh_export_record",
        "ams_soh_record_crc32",
        "ams_fuse_observer_default_config",
        "ams_fuse_observer_init_conservative",
        "ams_fuse_observer_update",
        "ams_fuse_typical_melt_time_s",
    ):
        require(symbol in link_map, f"linked power-core symbol missing: {symbol}")

    print("PASS: exact v2.6.27 SoP/SoH/fuse core contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
