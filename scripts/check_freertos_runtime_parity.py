#!/usr/bin/env python3

import argparse
import re
from pathlib import Path
import sys


EXPECTED = {
    "AMS_PERIOD_SAFETY_MS": 50,
    "AMS_PERIOD_CURRENT_MS": 20,
    "AMS_PERIOD_ADBMS_MS": 100,
    "AMS_PERIOD_CAN_MS": 100,
    "AMS_PERIOD_ESTIMATOR_MS": 100,
    "AMS_PERIOD_FAN_MS": 200,
    "AMS_PERIOD_AIR_MS": 500,
    "AMS_PERIOD_IMD_MS": 100,
    "AMS_HEARTBEAT_STARTUP_GRACE_MS": 3000,
    "AMS_HEARTBEAT_ADBMS_TIMEOUT_MS": 3000,
    "AMS_HEARTBEAT_CURRENT_TIMEOUT_MS": 200,
    "AMS_HEARTBEAT_TEMP_TIMEOUT_MS": 3000,
    "AMS_HEARTBEAT_CAN_TIMEOUT_MS": 2000,
    "AMS_HEARTBEAT_LOGGER_TIMEOUT_MS": 2000,
    "AMS_HEARTBEAT_IMD_TIMEOUT_MS": 500,
    "AMS_HEARTBEAT_FAN_TIMEOUT_MS": 1000,
    "AMS_HEARTBEAT_ESTIMATOR_TIMEOUT_MS": 500,
    "AMS_ADBMS_MUTEX_TIMEOUT_MS": 500,
    "AMS_CURRENT_WINDOW_MUTEX_TIMEOUT_MS": 20,
    "AMS_RUNTIME_AIR_ENABLED": 0,
    "AMS_RUNTIME_IMD_ENABLED": 0,
    "AMS_RUNTIME_ESTIMATOR_SAFETY_REQUIRED": 0,
}

PRIOS = [
    "AMS_PRIO_SAFETY",
    "AMS_PRIO_CURRENT",
    "AMS_PRIO_ADBMS",
    "AMS_PRIO_CAN",
    "AMS_PRIO_ESTIMATOR",
    "AMS_PRIO_FAN",
    "AMS_PRIO_AIR",
    "AMS_PRIO_IMD",
    "AMS_PRIO_DIAGNOSTICS",
]

STACK_PAIRS = [
    ("AMS_STACK_SAFETY", "AMS_ORACLE_STACK_SAFETY_BYTES"),
    ("AMS_STACK_CURRENT", "AMS_ORACLE_STACK_CURRENT_BYTES"),
    ("AMS_STACK_ADBMS", "AMS_ORACLE_STACK_ADBMS_BYTES"),
    ("AMS_STACK_CAN", "AMS_ORACLE_STACK_CAN_BYTES"),
    ("AMS_STACK_ESTIMATOR", "AMS_ORACLE_STACK_ESTIMATOR_BYTES"),
    ("AMS_STACK_FAN", "AMS_ORACLE_STACK_FAN_BYTES"),
    ("AMS_STACK_AIR", "AMS_ORACLE_STACK_AIR_BYTES"),
    ("AMS_STACK_IMD", "AMS_ORACLE_STACK_IMD_BYTES"),
    ("AMS_STACK_DIAGNOSTICS", "AMS_ORACLE_STACK_DIAGNOSTICS_BYTES"),
]


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def macro(text: str, name: str) -> int:
    m = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)U?\s*$",
        text,
        flags=re.MULTILINE,
    )
    if not m:
        fail(f"missing numeric macro {name}")
    return int(m.group(1))


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", type=Path)
    args = ap.parse_args()

    repo = args.repo_root.resolve()
    runtime = repo / "app" / "src" / "ams_threads.c"
    safety = repo / "app" / "src" / "ams_safety.c"
    kconfig = repo / "app" / "Kconfig"
    doc = repo / "docs" / "migration" / "Z010_FREERTOS_RUNTIME_PARITY.md"

    for path in (runtime, safety, kconfig, doc):
        require(path.is_file(), f"missing {path}")

    r = runtime.read_text(encoding="utf-8")
    s = safety.read_text(encoding="utf-8")
    k = kconfig.read_text(encoding="utf-8")

    vals = {name: macro(r, name) for name in set(EXPECTED) | set(PRIOS) |
            {x for pair in STACK_PAIRS for x in pair}}

    for name, expected in EXPECTED.items():
        require(vals[name] == expected,
                f"{name}: expected v2.6.27 value {expected}, got {vals[name]}")

    require(vals["AMS_PRIO_SAFETY"] < vals["AMS_PRIO_CURRENT"],
            "safety/current priority order drift")
    require(vals["AMS_PRIO_CURRENT"] < vals["AMS_PRIO_ADBMS"],
            "current/ADBMS priority order drift")
    require(vals["AMS_PRIO_ADBMS"] < vals["AMS_PRIO_CAN"],
            "ADBMS/CAN priority order drift")
    require(vals["AMS_PRIO_CAN"] < vals["AMS_PRIO_ESTIMATOR"],
            "CAN/estimator priority order drift")
    require(vals["AMS_PRIO_ESTIMATOR"] < vals["AMS_PRIO_FAN"],
            "estimator/fan priority order drift")
    require(vals["AMS_PRIO_FAN"] == vals["AMS_PRIO_AIR"],
            "fan/AIR priority equivalence drift")
    require(vals["AMS_PRIO_FAN"] < vals["AMS_PRIO_IMD"],
            "fan/IMD priority order drift")
    require(vals["AMS_PRIO_IMD"] < vals["AMS_PRIO_DIAGNOSTICS"],
            "IMD/diagnostics priority order drift")

    for zephyr_name, oracle_name in STACK_PAIRS:
        require(vals[zephyr_name] >= vals[oracle_name],
                f"{zephyr_name} below v2.6.27 byte-equivalent lower bound")

    # Current migration image must be more restrictive than operating firmware.
    require('BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY)' in s,
            "compile-time BMS authority lock missing")
    require('BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BALANCE_AUTHORITY)' in s,
            "compile-time balance authority lock missing")
    require('default n' in k and 'config AMS_BMS_AUTHORITY' in k,
            "BMS authority Kconfig default-off gate missing")
    require('config AMS_BALANCE_AUTHORITY' in k,
            "balance authority Kconfig gate missing")

    # Fatal policy must force low before halting.
    fatal = s.find("void k_sys_fatal_error_handler")
    require(fatal >= 0, "fatal handler missing")
    force = s.find("ams_bms_ok_force_low_direct();", fatal)
    halt = s.find("k_fatal_halt(reason);", fatal)
    require((force >= 0) and (halt > force),
            "fatal path must force BMS_OK low before halt")

    # A disabled placeholder must never look like a live physical safety source.
    require(".enabled = AMS_RUNTIME_AIR_ENABLED != 0U" in r,
            "AIR placeholder is not gated")
    require(".enabled = AMS_RUNTIME_IMD_ENABLED != 0U" in r,
            "IMD placeholder is not gated")
    require("if (!thread->enabled || (thread->stale_deadline_ms == 0U))" in r,
            "disabled/non-heartbeat stale suppression missing")
    require(".safety_evidence_ready = true" not in r,
            "placeholder runtime cycles are being treated as safety evidence")

    # Supervisor is activated first once the common startup epoch exists.
    epoch = r.find("&runtime_start_ms,")
    sup = r.find("start_thread_if_enabled(AMS_THREAD_SAFETY);")
    cur = r.find("start_thread_if_enabled(AMS_THREAD_CURRENT);")
    require((epoch >= 0) and (sup > epoch) and (cur > sup),
            "runtime start order does not establish epoch -> supervisor -> workers")

    # Keep the intentional scheduling divergence explicit rather than hidden.
    require("K_TIMEOUT_ABS_MS" in r,
            "absolute Zephyr release scheduling unexpectedly removed")
    require("skip missed historical releases" in r.lower(),
            "missed-release policy is no longer documented in source")

    print("PASS: Z-010 FreeRTOS v2.6.27 runtime/safety parity contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
