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


def symbol_enabled(config: str, symbol: str) -> bool:
    return re.search(rf"^{re.escape(symbol)}=y$", config, re.MULTILINE) is not None


def symbol_disabled(config: str, symbol: str) -> bool:
    return not symbol_enabled(config, symbol)


def dts_block(text: str, label: str) -> str:
    start = text.find(label)
    require(start >= 0, f"missing generated DTS block: {label}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed generated DTS block: {label}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{": depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0: return text[start:index + 1]
    fail(f"unterminated generated DTS block: {label}")


def kconfig_block(kconfig: str, symbol: str) -> str:
    marker = f"config {symbol}"
    start = kconfig.find(marker)
    require(start >= 0, f"hidden capability missing from Kconfig: {symbol}")
    next_config = kconfig.find("\nconfig ", start + len(marker))
    next_menu_end = kconfig.find("\nendmenu", start + len(marker))
    candidates = [x for x in (next_config, next_menu_end) if x >= 0]
    end = min(candidates) if candidates else len(kconfig)
    return kconfig[start:end]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    repo = args.repo_root.resolve(); build = args.build_dir.resolve()
    kconfig_path = repo / "app/Kconfig"; runtime_path = repo / "app/src/ams_threads.c"
    config_path = build / "zephyr/.config"; dts_path = build / "zephyr/zephyr.dts"
    for path in (kconfig_path, runtime_path, config_path, dts_path):
        require(path.is_file(), f"missing capability-contract artifact: {path}")
    kconfig=kconfig_path.read_text(); runtime=runtime_path.read_text(); config=config_path.read_text(errors="replace"); dts=dts_path.read_text(errors="replace")
    z017=symbol_enabled(config,"CONFIG_AMS_Z017_CELL_VALIDATION")
    z018 = symbol_enabled(config, "CONFIG_AMS_Z018_TEMP_VALIDATION")
    require(not (z017 and z018), "Z017/Z018 profiles cannot coexist")
    z017 = z017 or z018
    z016=symbol_enabled(config,"CONFIG_AMS_Z016_LINK_PROBE")
    require(not (z016 and z017), "Z016 and Z017 profiles cannot coexist")

    require(symbol_enabled(config,"CONFIG_AMS_CAP_CURRENT_ACTOR_LIVE") == symbol_enabled(config,"CONFIG_AMS_Z022_MEASUREMENT_VALIDATION"), "current actor must match Z022")
    always_enabled=(
        "CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT","CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT","CONFIG_AMS_CAP_FAN_ACTOR_LIVE","CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_IMD_CAPTURE_ADAPTER_PRESENT","CONFIG_AMS_CAP_IMD_ACTOR_LIVE","CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT","CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT",
    )
    always_disabled=(
        "CONFIG_AMS_CAP_CURRENT_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED","CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE","CONFIG_AMS_CAP_CAN_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_CAN_ACTOR_LIVE","CONFIG_AMS_CAP_CAN_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_FAN_PHYSICAL_VALIDATED","CONFIG_AMS_CAP_IMD_PHYSICAL_VALIDATED",
        "CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE","CONFIG_AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED",
    )
    for symbol in always_enabled: require(symbol_enabled(config,symbol), f"required capability disabled: {symbol}")
    for symbol in always_disabled: require(symbol_disabled(config,symbol), f"deferred capability unexpectedly enabled: {symbol}")
    require(symbol_enabled(config,"CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE") == z017,
            "ADBMS actor-live capability must exactly match Z017 owner profile")
    require(symbol_enabled(config,"CONFIG_AMS_CAP_ADBMS_MONITOR_ACQUISITION_LIVE") == z017,
            "ADBMS monitor-acquisition capability must exactly match Z017 profile")

    require(symbol_enabled(config,"CONFIG_AMS_CAP_TEMPERATURE_ACQUISITION_LIVE") == z018,
            "temperature acquisition must exactly match Z018")
    hidden = always_enabled + always_disabled + (
        "CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE","CONFIG_AMS_CAP_ADBMS_MONITOR_ACQUISITION_LIVE","CONFIG_AMS_CAP_TEMPERATURE_ACQUISITION_LIVE","CONFIG_AMS_CAP_WATCHDOG_ACTIVE")
    for symbol in (s.removeprefix("CONFIG_") for s in hidden):
        block=kconfig_block(kconfig,symbol)
        first=block.splitlines()[0].removeprefix(f"config {symbol}").strip()
        require(first=="" and not any(line.strip().startswith(('bool "','tristate "')) for line in block.splitlines()[1:]),
                f"migration capability must not expose a user prompt: {symbol}")

    actor_block=kconfig_block(kconfig,"AMS_CAP_ADBMS_ACTOR_LIVE")
    acq_block=kconfig_block(kconfig,"AMS_CAP_ADBMS_MONITOR_ACQUISITION_LIVE")
    require("default y if AMS_Z017_CELL_VALIDATION || AMS_Z018_TEMP_VALIDATION" in actor_block and "default y\n" not in actor_block,
            "actor capability may only be promoted conditionally by Z017")
    require("default y if AMS_Z017_CELL_VALIDATION || AMS_Z018_TEMP_VALIDATION" in acq_block and "default y\n" not in acq_block,
            "monitor-acquisition capability may only be promoted conditionally by Z017")

    fan_desc=runtime[runtime.find("[AMS_THREAD_FAN]"):runtime.find("[AMS_THREAD_AIR]")]
    imd_desc=runtime[runtime.find("[AMS_THREAD_IMD]"):runtime.find("[AMS_THREAD_DIAGNOSTICS]")]
    current_desc=runtime[runtime.find("[AMS_THREAD_CURRENT]"):runtime.find("[AMS_THREAD_ADBMS]")]
    adbms_desc=runtime[runtime.find("[AMS_THREAD_ADBMS]"):runtime.find("[AMS_THREAD_CAN]")]
    require(".safety_evidence_ready = true" in fan_desc,"fan evidence descriptor mismatch")
    require(".safety_evidence_ready = true" in imd_desc,"IMD evidence descriptor mismatch")
    require(".safety_evidence_ready = false" in current_desc,"current actor must remain deferred")
    require(".safety_evidence_ready = false" in adbms_desc,"Z017 acquisition is not ADBMS safety evidence")
    require("watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_ADBMS" not in runtime,
            "Z017 must not fabricate ADBMS watchdog safety heartbeat evidence")

    require('status = "okay"' in dts_block(dts,"iwdg:"),"IWDG device must remain present")
    require('status = "okay"' in dts_block(dts,"ams_adbms_interface:"),"typed ADBMS interface must remain enabled")
    require('status = "disabled"' in dts_block(dts,"spi6:"),"stock SPI6 device must remain disabled")
    validation=symbol_enabled(config,"CONFIG_AMS_IWDG_VALIDATION_MODE")
    require(symbol_enabled(config,"CONFIG_AMS_CAP_WATCHDOG_ACTIVE") == validation,"watchdog-active capability mismatch")
    for sym in ("CONFIG_AMS_WATCHDOG_TARGET_VALIDATED","CONFIG_AMS_IMD_TARGET_VALIDATED","CONFIG_AMS_FAN_TARGET_VALIDATED",
                "CONFIG_AMS_BMS_AUTHORITY","CONFIG_AMS_BALANCE_AUTHORITY"):
        require(symbol_disabled(config,sym), f"no-authority/nonphysical profile violated: {sym}")

    stage="Z-017" if z017 else ("Z-016" if z016 else "base")
    print(f"PASS: explicit migration-capability contract ({stage})")
    return 0

if __name__ == "__main__": sys.exit(main())
