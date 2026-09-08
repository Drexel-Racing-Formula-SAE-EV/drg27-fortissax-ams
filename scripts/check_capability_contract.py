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
    # Kconfiglib may omit hidden bool symbols whose effective value is n.
    # Match exact .config assignments so similarly-prefixed symbols cannot
    # create a false positive.
    return re.search(rf"^{re.escape(symbol)}=y$", config, re.MULTILINE) is not None


def symbol_disabled(config: str, symbol: str) -> bool:
    # For a Kconfig bool, absence from .config is an effective n.  Hidden
    # default-n migration facts are commonly omitted entirely by Kconfiglib,
    # so requiring a literal '# CONFIG_FOO is not set' line is incorrect.
    return not symbol_enabled(config, symbol)


def dts_block(text: str, label: str) -> str:
    start = text.find(label)
    require(start >= 0, f"missing generated DTS block: {label}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed generated DTS block: {label}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
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

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    kconfig_path = repo / "app" / "Kconfig"
    runtime_path = repo / "app" / "src" / "ams_threads.c"
    config_path = build / "zephyr" / ".config"
    dts_path = build / "zephyr" / "zephyr.dts"

    for path in (kconfig_path, runtime_path, config_path, dts_path):
        require(path.is_file(), f"missing capability-contract artifact: {path}")

    kconfig = kconfig_path.read_text(encoding="utf-8")
    runtime = runtime_path.read_text(encoding="utf-8")
    config = config_path.read_text(encoding="utf-8", errors="replace")
    dts = dts_path.read_text(encoding="utf-8", errors="replace")

    enabled = (
        "CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_FAN_ACTOR_LIVE",
        "CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_IMD_CAPTURE_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_IMD_ACTOR_LIVE",
        "CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT",
    )
    disabled = (
        "CONFIG_AMS_CAP_CURRENT_ACTOR_LIVE",
        "CONFIG_AMS_CAP_CURRENT_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE",
        "CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_CAN_ADAPTER_PRESENT",
        "CONFIG_AMS_CAP_CAN_ACTOR_LIVE",
        "CONFIG_AMS_CAP_CAN_SAFETY_EVIDENCE",
        "CONFIG_AMS_CAP_FAN_PHYSICAL_VALIDATED",
        "CONFIG_AMS_CAP_IMD_PHYSICAL_VALIDATED",
        "CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE",
        "CONFIG_AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED",
    )

    for symbol in enabled:
        require(symbol_enabled(config, symbol),
                f"Z-014 capability must be enabled: {symbol}")
    for symbol in disabled:
        require(symbol_disabled(config, symbol),
                f"Z-014 capability must remain disabled: {symbol}")

    # Capability symbols are hidden migration facts, not user-selectable knobs.
    hidden_capabilities = enabled + disabled + ("CONFIG_AMS_CAP_WATCHDOG_ACTIVE",)
    for symbol in tuple(s.removeprefix("CONFIG_") for s in hidden_capabilities):
        block = kconfig_block(kconfig, symbol)
        first_line_tail = block.splitlines()[0].removeprefix(f"config {symbol}").strip()
        require(first_line_tail == "" and not any(
            line.strip().startswith(('bool "', 'tristate "'))
            for line in block.splitlines()[1:]
        ), f"migration capability must not expose a user prompt: {symbol}")

    fan_desc = runtime[runtime.find("[AMS_THREAD_FAN]"):runtime.find("[AMS_THREAD_AIR]")]
    imd_desc = runtime[runtime.find("[AMS_THREAD_IMD]"):runtime.find("[AMS_THREAD_DIAGNOSTICS]")]
    current_desc = runtime[runtime.find("[AMS_THREAD_CURRENT]"):runtime.find("[AMS_THREAD_ADBMS]")]
    require(".safety_evidence_ready = true" in fan_desc,
            "fan capability says evidence-ready but runtime descriptor disagrees")
    require(".safety_evidence_ready = true" in imd_desc,
            "IMD capability says evidence-ready but runtime descriptor disagrees")
    require(".safety_evidence_ready = false" in current_desc,
            "current capability says deferred but runtime descriptor claims evidence")

    require('status = "okay"' in dts_block(dts, "iwdg:"),
            "Z-014 watchdog adapter requires an enabled IWDG device node")
    validation_mode = symbol_enabled(config, "CONFIG_AMS_IWDG_VALIDATION_MODE")
    require(symbol_enabled(config, "CONFIG_AMS_CAP_WATCHDOG_ACTIVE") == validation_mode,
            "watchdog-active capability must exactly match explicit validation mode")
    require(symbol_disabled(config, "CONFIG_AMS_WATCHDOG_TARGET_VALIDATED"),
            "Z-014 source/build closeout must not claim physical watchdog validation")
    require(symbol_disabled(config, "CONFIG_AMS_IMD_TARGET_VALIDATED"),
            "live IMD actor must not be confused with physical IMD validation")
    require(symbol_disabled(config, "CONFIG_AMS_FAN_TARGET_VALIDATED"),
            "live fan actor must not be confused with physical fan validation")
    require(symbol_disabled(config, "CONFIG_AMS_BMS_AUTHORITY"),
            "capability migration must not grant BMS authority")
    require(symbol_disabled(config, "CONFIG_AMS_BALANCE_AUTHORITY"),
            "capability migration must not grant balancing authority")

    print("PASS: Z-014 explicit migration-capability contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
