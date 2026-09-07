#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path
import sys

# Frozen exact DER26 AMS v2.6.27 / FW0.5.30 source hashes.
ORACLE_SHA256 = {
    "current_sensor.h": "5d713f2d484078503e3769cdfb37481766507ff9199926a9fde5b498eb7c8906",
    "current_sensor.c": "01883c0466375cfb97446b141496f0c2a63037aad585329718ba33742c8eb12a",
    "current_fault.h": "12c94c1da87ea7de9ba40a634fb0f146af48a83485022e861ea5907b2a8c9d7a",
    "current_fault.c": "613291629395912918b39f2aa661dbd15eb9a7056dc13f147353d905334f7cfb",
    "current_task.c": "b0f26b71426af014f94fe2803f6654cf9f2bda0a8af68ed1612ff086593ab0f2",
    "stm32f767z.h": "0977dc51084c07f028072475352aa350a50c17a13d59deaa16eb9cf15ffdeeab",
    "stm32f767z.c": "13928fb07994baae307f81eaa0752dea3ce3cf03aae8ccf4443b3ea926bd8f29",
    "board.c": "5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df",
    "main.c": "ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84",
}

# Z-011 approved portable adaptations. These lock the exact candidate that was
# differentially compared against the hashes above. current_fault is a literal
# oracle copy except include/header-guard adaptation. current_sensor removes
# only HAL ownership/read functions and injects the equivalent high/low raw
# freshness transaction.
ADAPTED_SHA256 = {
    "ams_current_sensor.h": "dd911404b08a56e4af0ab2ac7038f2a56b32444232249cf14ad5463ecd6b4fea",
    "ams_current_sensor.c": "7c1694738faec4bc3c4775b7b798a39890ce04aeb1e811b55fe04c89919db0ba",
    "ams_current_fault.h": "781d03f5707f53655356d5ef021c0dbc4b940930ac772c93f3ba797acfd56821",
    "ams_current_fault.c": "45ebf864dba3975e5b85c42db490515801383a5c85f982a15627e24b709cc018",
}


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    include = repo / "lib" / "ams_core" / "include" / "ams_core"
    current = repo / "lib" / "ams_core" / "current"
    faults = repo / "lib" / "ams_core" / "faults"

    files = {
        "ams_current_sensor.h": include / "ams_current_sensor.h",
        "ams_current_sensor.c": current / "ams_current_sensor.c",
        "ams_current_fault.h": include / "ams_current_fault.h",
        "ams_current_fault.c": faults / "ams_current_fault.c",
    }

    for name, path in files.items():
        require(path.is_file(), f"missing Z-011 current core file: {path}")
        actual = digest(path)
        require(
            actual == ADAPTED_SHA256[name],
            f"{name} drifted from the SIL/differential-validated Z-011 adaptation "
            f"(expected {ADAPTED_SHA256[name]}, got {actual})",
        )

    # Preserve an auditable record of exactly which legacy files established
    # the behavioral oracle. This is intentionally independent of later DER26
    # Git history.
    doc = repo / "docs" / "migration" / "Z011_CURRENT_ADC_PARITY.md"
    require(doc.is_file(), f"missing {doc}")
    doc_text = doc.read_text(encoding="utf-8")
    for name, sha in ORACLE_SHA256.items():
        require(sha in doc_text, f"oracle SHA-256 not documented for {name}")

    sensor_h = files["ams_current_sensor.h"].read_text(encoding="utf-8")
    sensor_c = files["ams_current_sensor.c"].read_text(encoding="utf-8")
    fault_h = files["ams_current_fault.h"].read_text(encoding="utf-8")
    fault_c = files["ams_current_fault.c"].read_text(encoding="utf-8")

    sensor_macros = {
        "CURRENT_SENSOR_CALIBRATION_MAGIC": "0x4943414Cu",
        "CURRENT_SENSOR_CALIBRATION_SCHEMA": "1u",
        "CURRENT_SENSOR_CALIBRATION_RECORD_SIZE": "44u",
        "CURRENT_SENSOR_CALIBRATION_UNCERTAINTY_UNKNOWN": "UINT16_MAX",
        "CURRENT_ADC_NOMINAL_VREF_V": "3.3f",
        "CURRENT_SENSOR_NOMINAL_SUPPLY_V": "5.0f",
        "CURRENT_ADC_MAX_COUNT": "4095.0f",
        "DHAB_CH_50A_SENS_V_PER_A_AT_5V": "0.040f",
        "DHAB_CH_800A_SENS_V_PER_A_AT_5V": "0.0025f",
        "SENSOR_DIVIDER_TOP_OHM": "100000.0f",
        "SENSOR_DIVIDER_BOTTOM_OHM": "150000.0f",
        "CURRENT_ADC_IMPLAUS_LOW_COUNT": "100u",
        "CURRENT_ADC_IMPLAUS_HIGH_COUNT": "3800u",
        "DHAB_SENSOR_VALID_MIN_V": "0.20f",
        "DHAB_SENSOR_VALID_MAX_V": "4.80f",
        "DHAB_SENSOR_CLAMP_LOW_V": "0.30f",
        "DHAB_SENSOR_CLAMP_HIGH_V": "4.70f",
        "CURRENT_50A_USE_LIMIT_A": "45.0f",
        "CURRENT_800A_RETURN_TO_50A_LIMIT_A": "38.0f",
        "CURRENT_CHANNEL_COMPARE_MIN_A": "10.0f",
        "CURRENT_CHANNEL_AGREE_ABS_A": "7.5f",
        "CURRENT_CHANNEL_AGREE_PCT": "0.15f",
        "CURRENT_50A_DEADBAND_A": "0.25f",
        "CURRENT_800A_DEADBAND_A": "2.0f",
        "CURRENT_FILTER_ALPHA": "0.25f",
        "CURRENT_ZERO_CAPTURE_MAX_50A_A": "5.0f",
        "CURRENT_ZERO_CAPTURE_MAX_800A_A": "25.0f",
        "CURRENT_CAL_CONFIDENT_50A_UNCERTAINTY_MA": "500u",
        "CURRENT_CAL_CONFIDENT_800A_UNCERTAINTY_MA": "5000u",
        "CURRENT_CAL_TEMP_MIN_DECI_C": "(-400)",
        "CURRENT_CAL_TEMP_MAX_DECI_C": "1200",
    }
    for name, value in sensor_macros.items():
        pattern = re.compile(
            rf"^\s*#define\s+{re.escape(name)}\s+{re.escape(value)}\s*$",
            re.MULTILINE,
        )
        require(pattern.search(sensor_c + "\n" + sensor_h) is not None,
                f"current-sensor oracle macro drift: {name}={value}")

    for token in (
        "0xEDB88320u",
        "current_sensor_adc_begin",
        "current_sensor_adc_publish_high",
        "current_sensor_adc_publish_low",
        "current_sensor_adc_finish",
    ):
        require(token in sensor_c or token in sensor_h,
                f"current-sensor oracle token missing: {token}")

    fault_macros = {
        "CURRENT_DIRECTION_DISCHARGE_POSITIVE": "1u",
        "CURRENT_SENSOR_STARTUP_IGNORE_MS": "250u",
        "CURRENT_SENSOR_FAULT_CONFIRM_MS": "250u",
        "CURRENT_DISCHARGE_WARN_A": "70.0f",
        "CURRENT_DISCHARGE_TRIP_A": "85.0f",
        "CURRENT_DISCHARGE_TRIP_MS": "500u",
        "CURRENT_DISCHARGE_FAST_TRIP_A": "120.0f",
        "CURRENT_DISCHARGE_FAST_TRIP_MS": "100u",
        "CURRENT_DISCHARGE_EXTREME_A": "240.0f",
        "CURRENT_CHARGE_WARN_A": "10.5f",
        "CURRENT_CHARGE_TRIP_A": "12.0f",
        "CURRENT_CHARGE_TRIP_MS": "500u",
        "CURRENT_CHARGE_FAST_TRIP_A": "15.0f",
        "CURRENT_CHARGE_FAST_TRIP_MS": "100u",
        "CURRENT_CHARGE_EXTREME_A": "30.0f",
        "CURRENT_REGEN_ENABLED_PLACEHOLDER": "0u",
        "CURRENT_REGEN_UNEXPECTED_A": "5.0f",
        "CURRENT_REGEN_WARN_A": "20.0f",
        "CURRENT_REGEN_TRIP_A": "25.0f",
        "CURRENT_REGEN_TRIP_MS": "500u",
        "CURRENT_REGEN_FAST_TRIP_A": "30.0f",
        "CURRENT_REGEN_FAST_TRIP_MS": "100u",
        "CURRENT_REGEN_EXTREME_A": "50.0f",
        "CURRENT_PRECHARGE_WARN_A": "0.8f",
        "CURRENT_PRECHARGE_TRIP_A": "1.2f",
        "CURRENT_PRECHARGE_TRIP_MS": "200u",
        "CURRENT_PRECHARGE_FAST_TRIP_A": "2.0f",
        "CURRENT_PRECHARGE_FAST_TRIP_MS": "40u",
    }
    for name, value in fault_macros.items():
        pattern = re.compile(
            rf"^\s*#define\s+{re.escape(name)}\s+{re.escape(value)}\s*$",
            re.MULTILINE,
        )
        require(pattern.search(fault_c + "\n" + fault_h) is not None,
                f"current-fault oracle macro drift: {name}={value}")

    forbidden = {
        "Zephyr include": re.compile(r'^\s*#\s*include\s*[<\"]zephyr/', re.MULTILINE),
        "FreeRTOS include": re.compile(r'^\s*#\s*include\s*[<\"]FreeRTOS', re.MULTILINE),
        "CMSIS-RTOS": re.compile(r"\bos(?:Thread|Mutex|Kernel)[A-Za-z0-9_]*\b"),
        "STM32 HAL": re.compile(r"\bHAL_[A-Za-z0-9_]+\b"),
        "STM32 handle": re.compile(r"\bADC_HandleTypeDef\b"),
        "dynamic allocation": re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\("),
    }
    for path in files.values():
        text = path.read_text(encoding="utf-8")
        for label, pattern in forbidden.items():
            match = pattern.search(text)
            require(match is None,
                    f"{label} leaked into portable current core {path.relative_to(repo)}")

    cmake = (repo / "lib" / "ams_core" / "CMakeLists.txt").read_text(encoding="utf-8")
    require("current/ams_current_sensor.c" in cmake,
            "portable current sensor is not linked into ams_core")
    require("faults/ams_current_fault.c" in cmake,
            "portable current fault policy is not linked into ams_core")

    dot_config = (build / "zephyr" / ".config").read_text(encoding="utf-8")
    link_map = (build / "zephyr" / "zephyr.map").read_text(encoding="utf-8")
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in dot_config,
            "Z-011 current core must remain no-authority")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in dot_config,
            "Z-011 current core must remain no-balance-authority")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in dot_config,
            "Z-011 current core must remain application-heap-free")

    for symbol in (
        "current_sensor_init",
        "current_sensor_convert",
        "current_sensor_calibration_record_crc32",
        "current_sensor_restore_calibration",
        "current_sensor_adc_begin",
        "current_sensor_adc_publish_high",
        "current_sensor_adc_publish_low",
        "current_sensor_adc_finish",
        "current_fault_init",
        "current_fault_update",
        "current_fault_reset_latch",
    ):
        require(symbol in link_map, f"linked current-core symbol missing: {symbol}")

    print("PASS: exact v2.6.27 current-sensor/fault behavioral contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
