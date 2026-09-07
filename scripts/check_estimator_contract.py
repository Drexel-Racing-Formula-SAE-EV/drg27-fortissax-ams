#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path
import sys


ORACLE_SHA256 = {
    "ams_soc_ekf.h":
        "209e62da3136aa0ccc3ea427560319e13ea2929226a4584b7381131f88db7b77",
    "ams_soc_ekf.c":
        "f277f28289f78c016ddf47bcf4fc0c68c0cf72a1d6fda00cc66f62b9cce0d928",
    "ams_estimator_lut.h":
        "c28062bccd31328a32be323df4d0ff33e81d311eb706ee8dee9b56aa950eb7fc",
    "ams_estimator_lut.c":
        "c452ce2662694fba0ea5f8361b420f42e9790164ba27528777703b182c8bf93f",
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def canonical_bytes(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")

    if path.name == "ams_soc_ekf.h":
        text = text.replace(
            "#include <ams_core/ams_estimator_config.h>",
            '#include "ams_build_profile.h"',
        )
    elif path.name == "ams_soc_ekf.c":
        text = text.replace(
            "#include <ams_core/ams_soc_ekf.h>",
            '#include "estimator/ams_soc_ekf.h"',
        ).replace(
            "#include <ams_core/ams_estimator_lut.h>",
            '#include "estimator/ams_estimator_lut.h"',
        )
    elif path.name == "ams_estimator_lut.c":
        text = text.replace(
            "#include <ams_core/ams_estimator_lut.h>",
            '#include "estimator/ams_estimator_lut.h"',
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
    estimator = core / "estimator"

    files = {
        "ams_soc_ekf.h": include / "ams_soc_ekf.h",
        "ams_soc_ekf.c": estimator / "ams_soc_ekf.c",
        "ams_estimator_lut.h": include / "ams_estimator_lut.h",
        "ams_estimator_lut.c": estimator / "ams_estimator_lut.c",
    }

    for path in files.values():
        require(path.is_file(), f"missing estimator oracle copy: {path}")

    # Strongest Z-009 invariant: after reversing only the approved include-path
    # substitutions, the four estimator files must hash exactly to the frozen
    # v2.6.27 / FW0.5.30 oracle files.
    for name, path in files.items():
        digest = hashlib.sha256(canonical_bytes(path)).hexdigest()
        require(
            digest == ORACLE_SHA256[name],
            f"{name} diverged from frozen v2.6.27 oracle "
            f"(expected {ORACLE_SHA256[name]}, got {digest})",
        )

    config_path = include / "ams_estimator_config.h"
    require(config_path.is_file(), f"missing {config_path}")
    config_text = config_path.read_text(encoding="utf-8")

    require(
        re.search(
            r"^#define\s+AMS_ESTIMATOR_TOPOLOGY_PACK\s+1$",
            config_text,
            re.MULTILINE,
        ) is not None,
        "pack topology identifier changed",
    )
    require(
        re.search(
            r"^#define\s+AMS_ESTIMATOR_TOPOLOGY_SEGMENTS\s+2$",
            config_text,
            re.MULTILINE,
        ) is not None,
        "segment topology identifier changed",
    )
    require(
        "#define AMS_ESTIMATOR_DEFAULT_TOPOLOGY "
        "AMS_ESTIMATOR_TOPOLOGY_PACK" in config_text,
        "portable default must preserve v2.6.27 BENCH -> PACK behavior",
    )

    header = files["ams_soc_ekf.h"].read_text(encoding="utf-8")
    source = files["ams_soc_ekf.c"].read_text(encoding="utf-8")
    lut_source = files["ams_estimator_lut.c"].read_text(encoding="utf-8")

    for token in (
        "#define AMS_EKF_MAX_INSTANCES       10U",
        "#define AMS_EKF_ADAPT_WIN           10U",
        "#define AMS_EKF_PACK_SERIES_GROUPS  75U",
        "#define AMS_EKF_PACK_PARALLEL_CELLS 6.0f",
        "#define AMS_EKF_CELL_CAPACITY_AH    4.2f",
        "#define AMS_EKF_DEFAULT_DT_S        0.1f",
        "#define AMS_EKF_DEFAULT_SOC_INIT    1.0f",
        "#define AMS_EKF_DEFAULT_R0_INIT_OHM 0.0147f",
        "float p_soc_vp1;",
        "float p_soc_vp2;",
        "float p_vp1_vp2;",
        "float innovation_variance_V2;",
        "ams_ekf_acquisition_t acquisition;",
    ):
        require(token in header, f"estimator contract token missing: {token}")

    for token in (
        "covariance_joseph_update",
        "covariance_sanitize",
        "update_adaptive_r",
        "ams_ekf_step_acquiring_gated",
        "ams_ekf_acquisition_observe",
        "ams_estimator_acquisition_resolve",
        "ams_estimator_cc_apply_charge",
        "ams_estimator_refresh_summary",
        "AMS_EKF_INV_TAU2",
        "AMS_EKF_R0_MIN_OHM",
        "AMS_EKF_R0_MAX_OHM",
    ):
        require(token in source, f"estimator algorithm token missing: {token}")

    require("s_OCV_table[303]" in lut_source, "OCV LUT size changed")
    require("s_R0_table[36]" in lut_source, "R0 LUT size changed")
    require("s_C1_table[36]" in lut_source, "C1 LUT size changed")
    require("s_R1C1_table[36]" in lut_source, "tau1 LUT size changed")

    forbidden_patterns = {
        "Zephyr API": re.compile(r"\b(?:k_mutex|k_sem|k_spin|k_sleep|k_work)\b"),
        "Zephyr include": re.compile(r'^\s*#\s*include\s*[<"]zephyr/', re.MULTILINE),
        "FreeRTOS API": re.compile(r"\b(?:xTask|vTask|taskENTER_|taskEXIT_)[A-Za-z0-9_]*"),
        "FreeRTOS include": re.compile(r'^\s*#\s*include\s*[<"]FreeRTOS', re.MULTILINE),
        "CMSIS-RTOS": re.compile(r"\bcmsis_os[0-9_]*\b|\bosThread[A-Za-z0-9_]*\b"),
        "STM32 HAL": re.compile(r"\bHAL_[A-Za-z0-9_]+\b"),
        "dynamic allocation": re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\("),
    }

    for path in (
        files["ams_soc_ekf.h"],
        files["ams_soc_ekf.c"],
        files["ams_estimator_lut.h"],
        files["ams_estimator_lut.c"],
        config_path,
    ):
        text = path.read_text(encoding="utf-8", errors="replace")
        for description, pattern in forbidden_patterns.items():
            match = pattern.search(text)
            require(
                match is None,
                f"{description} leaked into portable estimator "
                f"{path.relative_to(repo)}: {match.group(0) if match else ''}",
            )

    cmake = (core / "CMakeLists.txt").read_text(encoding="utf-8")
    require(
        "estimator/ams_estimator_lut.c" in cmake,
        "estimator LUT source is not part of ams_core",
    )
    require(
        "estimator/ams_soc_ekf.c" in cmake,
        "estimator EKF source is not part of ams_core",
    )

    dot_config = (build / "zephyr" / ".config").read_text(
        encoding="utf-8", errors="replace"
    )
    link_map = (build / "zephyr" / "zephyr.map").read_text(
        encoding="utf-8", errors="replace"
    )

    require(
        "# CONFIG_AMS_BMS_AUTHORITY is not set" in dot_config,
        "Z-009 must remain no-authority",
    )
    require(
        "# CONFIG_AMS_BALANCE_AUTHORITY is not set" in dot_config,
        "Z-009 must remain no-balance-authority",
    )
    require(
        "CONFIG_HEAP_MEM_POOL_SIZE=0" in dot_config,
        "Z-009 must remain application-heap-free",
    )

    # The target startup contract intentionally references these functions so
    # the target link proves both EKF and LUT objects are present.
    for symbol in (
        "ams_ekf_make_pack_config",
        "ams_ekf_init",
        "ams_ekf_step_gated",
        "ams_p42a_ocv_v",
        "ams_p42a_r0_ohm",
    ):
        require(symbol in link_map, f"linked estimator symbol missing: {symbol}")

    print("PASS: exact v2.6.27 estimator-core contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
