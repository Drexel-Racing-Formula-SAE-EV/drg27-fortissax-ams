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
    "AMS_ADBMS_MUTEX_TIMEOUT_MS": 500,
    "AMS_CURRENT_WINDOW_MUTEX_TIMEOUT_MS": 20,
    "AMS_RUNTIME_AIR_ENABLED": 0,
    "AMS_RUNTIME_IMD_ENABLED": 1,
    "AMS_RUNTIME_ESTIMATOR_SAFETY_REQUIRED": 0,
}

HEARTBEAT_EXPECTED = {
    "AMS_WATCHDOG_HEARTBEAT_ADBMS_TIMEOUT_MS": 3000,
    "AMS_WATCHDOG_HEARTBEAT_CURRENT_TIMEOUT_MS": 200,
    "AMS_WATCHDOG_HEARTBEAT_TEMP_TIMEOUT_MS": 3000,
    "AMS_WATCHDOG_HEARTBEAT_CAN_TIMEOUT_MS": 2000,
    "AMS_WATCHDOG_HEARTBEAT_LOGGER_TIMEOUT_MS": 2000,
    "AMS_WATCHDOG_HEARTBEAT_IMD_TIMEOUT_MS": 500,
    "AMS_WATCHDOG_HEARTBEAT_FAN_TIMEOUT_MS": 1000,
    "AMS_WATCHDOG_HEARTBEAT_ESTIMATOR_TIMEOUT_MS": 500,
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
    heartbeat_h = repo / "lib" / "ams_core" / "include" / "ams_core" / "ams_watchdog_heartbeat.h"
    heartbeat_c = repo / "lib" / "ams_core" / "watchdog" / "ams_watchdog_heartbeat.c"
    policy_h = repo / "lib" / "ams_core" / "include" / "ams_core" / "ams_watchdog_policy.h"
    policy_c = repo / "lib" / "ams_core" / "watchdog" / "ams_watchdog_policy.c"
    safety = repo / "app" / "src" / "ams_safety.c"
    fail_low = repo / "boards" / "drexel" / "der26_ams" / "ams_fail_low_stm32.c"
    bms_adapter = repo / "drivers" / "ams" / "bms_ok_zephyr.c"
    kconfig = repo / "app" / "Kconfig"
    prj = repo / "app" / "prj.conf"
    defconfig = repo / "boards" / "drexel" / "der26_ams" / "der26_ams_defconfig"
    doc = repo / "docs" / "migration" / "Z013_IMD_CAPTURE_PARITY.md"

    for path in (runtime, heartbeat_h, heartbeat_c, policy_h, policy_c, safety, fail_low,
                 bms_adapter, kconfig, prj, defconfig, doc):
        require(path.is_file(), f"missing {path}")

    r = runtime.read_text(encoding="utf-8")
    hh = heartbeat_h.read_text(encoding="utf-8")
    hc = heartbeat_c.read_text(encoding="utf-8")
    ph = policy_h.read_text(encoding="utf-8")
    pc = policy_c.read_text(encoding="utf-8")
    s = safety.read_text(encoding="utf-8")
    f = fail_low.read_text(encoding="utf-8")
    b = bms_adapter.read_text(encoding="utf-8")
    k = kconfig.read_text(encoding="utf-8")
    pconf = prj.read_text(encoding="utf-8")
    dconf = defconfig.read_text(encoding="utf-8")

    vals = {name: macro(r, name) for name in set(EXPECTED) | set(PRIOS) |
            {x for pair in STACK_PAIRS for x in pair}}

    for name, expected in EXPECTED.items():
        require(vals[name] == expected,
                f"{name}: expected v2.6.27 value {expected}, got {vals[name]}")

    require(macro(ph, "AMS_WATCHDOG_STARTUP_GRACE_MS") == 3000,
            "watchdog policy startup grace drifted from v2.6.27")
    require(
        re.search(
            r"^\s*#define\s+AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS\s+"
            r"AMS_WATCHDOG_STARTUP_GRACE_MS\s*$",
            hh,
            flags=re.MULTILINE,
        ) is not None,
        "heartbeat startup grace must alias watchdog policy grace",
    )
    for name, expected in HEARTBEAT_EXPECTED.items():
        actual = macro(hh, name)
        require(actual == expected,
                f"{name}: expected v2.6.27 value {expected}, got {actual}")

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

    # v2.6.27 uses configASSERT and stack-overflow checking. The Zephyr
    # migration must refuse to build if its corresponding integrity guards
    # are weakened.
    for token in (
        'BUILD_ASSERT(IS_ENABLED(CONFIG_ASSERT)',
        'BUILD_ASSERT(IS_ENABLED(CONFIG_ARM_MPU)',
        'BUILD_ASSERT(IS_ENABLED(CONFIG_HW_STACK_PROTECTION)',
        'BUILD_ASSERT(IS_ENABLED(CONFIG_THREAD_STACK_INFO)',
        'BUILD_ASSERT(IS_ENABLED(CONFIG_INIT_STACKS)',
        'BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == 0',
    ):
        require(token in s, f"compile-time integrity invariant missing: {token}")
    require("CONFIG_ASSERT=y" in pconf,
            "kernel assertions are not enabled in prj.conf")
    require("CONFIG_THREAD_STACK_INFO=y" in pconf,
            "thread stack metadata is not enabled in prj.conf")
    require("CONFIG_INIT_STACKS=y" in pconf,
            "initialized stack watermarking is not enabled in prj.conf")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in pconf,
            "application heap is not disabled in prj.conf")
    require("CONFIG_ARM_MPU=y" in dconf,
            "ARM MPU is not enabled in board defconfig")
    require("CONFIG_HW_STACK_PROTECTION=y" in dconf,
            "hardware stack protection is not enabled in board defconfig")

    # Fatal policy must force low before halting.
    fatal = s.find("void k_sys_fatal_error_handler")
    require(fatal >= 0, "fatal handler missing")
    force = s.find("ams_bms_ok_force_low_direct();", fatal)
    halt = s.find("k_fatal_halt(reason);", fatal)
    require((force >= 0) and (halt > force),
            "fatal path must force BMS_OK low before halt")
    direct_start = f.find("void ams_bms_ok_force_low_direct")
    direct = f[direct_start:]
    require(direct_start >= 0,
            "board-specific direct fail-low implementation missing")
    require("__DSB();" in direct and "__ISB();" in direct,
            "direct fail-low path must complete with DSB+ISB barriers")
    require("RCC->" not in s and "GPIOE->" not in s and "<soc.h>" not in s,
            "application safety policy regained direct STM32 register ownership")
    require("zephyr/drivers/" not in s and "DT_NODELABEL" not in s,
            "application safety policy regained normal hardware mapping ownership")
    require("SYS_INIT(ams_bms_ok_early_init, PRE_KERNEL_1, 0);" in f,
            "pre-kernel fail-low registration left the board safety layer")
    require("GPIO_DT_SPEC_GET(AMS_SAFETY_NODE, bms_ok_gpios)" in b,
            "normal BMS_OK path is not isolated behind typed Zephyr GPIO adapter")

    # AIR remains a disabled placeholder. Z-013 promotes IMD to a real
    # workload, but that is no-authority migration evidence only and does not
    # claim the source's AMS_IMD_TARGET_VALIDATED vehicle gate.
    require(".enabled = AMS_RUNTIME_AIR_ENABLED != 0U" in r,
            "AIR placeholder is not gated")
    require(".enabled = AMS_RUNTIME_IMD_ENABLED != 0U" in r,
            "IMD runtime enable is not explicit")
    require("if (!thread->enabled || (thread->stale_deadline_ms == 0U))" in r,
            "disabled/non-heartbeat stale suppression missing")
    fan_start = r.find("[AMS_THREAD_FAN]")
    air_start = r.find("[AMS_THREAD_AIR]")
    imd_start = r.find("[AMS_THREAD_IMD]")
    diag_start = r.find("[AMS_THREAD_DIAGNOSTICS]")
    require((fan_start >= 0) and (air_start > fan_start), "fan descriptor missing")
    require((imd_start >= 0) and (diag_start > imd_start), "IMD descriptor missing")
    fan_block = r[fan_start:air_start]
    imd_block = r[imd_start:diag_start]
    require(".safety_evidence_ready = true" in fan_block,
            "real fan workload is not safety-liveness evidence")
    require(".safety_evidence_ready = true" in imd_block,
            "real Z-013 IMD workload is not safety-liveness evidence")
    require(".safety_heartbeat_required = AMS_RUNTIME_IMD_ENABLED != 0U" in imd_block,
            "IMD heartbeat membership drift")
    non_real = r[:fan_start] + r[air_start:imd_start] + r[diag_start:]
    require(".safety_evidence_ready = true" not in non_real,
            "unmigrated placeholder runtime cycle is treated as safety evidence")

    # v2.6.27 initializes heartbeat grace before its safety-critical RTOS
    # objects, then lets the highest-priority supervisor run first.
    start_fn = r.find("int ams_threads_start(void)")
    epoch = r.find("&runtime_start_ms,", start_fn)
    create = r.find("create_thread(", start_fn)
    sup = r.find("start_thread_if_enabled(AMS_THREAD_SAFETY);", start_fn)
    cur = r.find("start_thread_if_enabled(AMS_THREAD_CURRENT);", start_fn)
    hb_init = r.find("ams_watchdog_heartbeat_init(", epoch)
    arm = r.find("ams_watchdog_platform_start();", hb_init)
    require((epoch >= 0) and (hb_init > epoch) and (arm > hb_init) and
            (create > arm) and (sup > create) and (cur > sup),
            "runtime start order must be epoch -> heartbeat -> IWDG arm -> create -> supervisor -> workers")

    # Exact watchdog heartbeat boundary/count semantics now live in the
    # portable monitor; generic runtime stale flags remain diagnostic only.
    require("prior_count != UINT32_MAX" in hc,
            "watchdog heartbeat count no longer saturates at UINT32_MAX")
    require("if (!startup_grace)" in hc,
            "unseen watchdog heartbeat is not stale at exact grace expiry")
    require("now_ms - monitor->last_ms[i]) > timeout_ms" in hc,
            "seen watchdog heartbeat must remain fresh at exact timeout equality")
    require("now_ms - monitor->boot_ms" in hc,
            "watchdog heartbeat elapsed time lost uint32 wrap semantics")
    require("input->now_ms - input->boot_ms" in pc,
            "watchdog policy startup grace lost uint32 wrap semantics")
    require("watchdog_heartbeat_snapshot_get" in r and
            "watchdog_migrated_stale_mask_value" not in r,
            "watchdog feed evidence must come from explicit portable heartbeat monitor")
    require("atomic_increment_saturating_u32" in r,
            "generic runtime diagnostic heartbeat count no longer saturates")
    require("(startup_age_ms >= thread->startup_grace_ms)" in r,
            "generic unseen runtime boundary drift")
    require("snapshot->heartbeat_age_ms <" in r,
            "diagnostic startup-grace boundary drift")

    # Safety and fan are real periodic workloads and reproduce the FreeRTOS
    # osDelayUntil(entry + period) overrun behavior: retry immediately after an
    # overrun and re-anchor from the next actual entry.
    require("next_release_ms = start_ms + thread->period_ms;" in r,
            "real workload release is not anchored to actual entry")
    require("release_ms = complete_ms;" in r,
            "real workload overrun does not retry immediately")

    # Placeholder-only workers intentionally retain absolute release scheduling
    # until their actual FreeRTOS task bodies are migrated.
    require("skip missed historical releases" in r.lower(),
            "placeholder missed-release policy is no longer explicit")

    # Current remains deferred. Z-013 promotes fan and IMD only;
    # current/ADBMS/CAN/estimator placeholders remain non-safety evidence.
    require("ams_current_adc_read_pair" not in r,
            "scope drift: runtime current thread already acquires ADC")
    require("reads current ADCs" in r,
            "runtime source no longer documents deferred current integration")
    require(".safety_evidence_ready = false" in r,
            "placeholder safety-evidence lock missing")

    imd_worker_start = r.find("static void imd_thread_entry")
    imd_worker_end = r.find("static void runtime_update_stale_flags", imd_worker_start)
    imd_worker = r[imd_worker_start:imd_worker_end]
    require("ams_imd_capture_read_at" in imd_worker,
            "real IMD worker does not read the capture adapter")
    fail_low = imd_worker.find("ams_bms_ok_force_low_direct();")
    watchdog_kick = imd_worker.find("watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_IMD")
    heartbeat = imd_worker.find("runtime_publish_complete(")
    require(0 <= fail_low < watchdog_kick < heartbeat,
            "IMD watchdog/generic heartbeat must be published after fail-low handling")
    require("next_release_ms = start_ms + thread->period_ms;" in imd_worker,
            "IMD osDelayUntil(entry+100ms) parity drift")

    print("PASS: Z-014 FreeRTOS v2.6.27 runtime/watchdog safety parity contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
