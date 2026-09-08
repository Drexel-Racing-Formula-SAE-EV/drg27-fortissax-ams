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
    # Kconfiglib commonly omits hidden bool symbols whose effective value is n.
    return not symbol_enabled(config, symbol)


def function_block(text: str, signature: str) -> str:
    # Skip forward declarations: only accept an occurrence whose opening brace
    # appears before the next semicolon.
    search_from = 0
    while True:
        start = text.find(signature, search_from)
        require(start >= 0, f"missing function: {signature}")
        brace = text.find("{", start)
        semicolon = text.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            break
        search_from = start + len(signature)

    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated function: {signature}")


def parse_numeric_macro(text: str, name: str) -> int:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)[uU]?\s*$",
        text,
        re.MULTILINE,
    )
    require(match is not None, f"missing/non-numeric watchdog macro: {name}")
    return int(match.group(1))


def production_c_sources(repo: Path):
    for source_root in ("app", "boards", "drivers", "lib", "zephyr"):
        root = repo / source_root
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*.c")):
            if "tests" in path.parts:
                continue
            yield path, path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    policy_h = repo / "lib/ams_core/include/ams_core/ams_watchdog_policy.h"
    policy_c = repo / "lib/ams_core/watchdog/ams_watchdog_policy.c"
    heartbeat_h = repo / "lib/ams_core/include/ams_core/ams_watchdog_heartbeat.h"
    heartbeat_c = repo / "lib/ams_core/watchdog/ams_watchdog_heartbeat.c"
    stack_h = repo / "lib/ams_core/include/ams_core/ams_stack_health.h"
    stack_c = repo / "lib/ams_core/integrity/ams_stack_health.c"
    core_cmake = repo / "lib/ams_core/CMakeLists.txt"
    platform_h = repo / "include/ams_platform/watchdog.h"
    adapter_c = repo / "drivers/ams/watchdog_zephyr.c"
    runtime_c = repo / "app/src/ams_threads.c"
    kconfig_p = repo / "app/Kconfig"
    dts_p = repo / "boards/drexel/der26_ams/der26_ams.dts"
    config_p = build / "zephyr/.config"
    generated_dts_p = build / "zephyr/zephyr.dts"
    map_p = build / "zephyr/zephyr.map"

    artifacts = (
        policy_h, policy_c, heartbeat_h, heartbeat_c, stack_h, stack_c,
        core_cmake, platform_h, adapter_c, runtime_c, kconfig_p, dts_p,
        config_p, generated_dts_p, map_p,
    )
    for path in artifacts:
        require(path.is_file(), f"missing watchdog-contract artifact: {path}")

    ph = policy_h.read_text(encoding="utf-8")
    pc = policy_c.read_text(encoding="utf-8")
    hh = heartbeat_h.read_text(encoding="utf-8")
    hc = heartbeat_c.read_text(encoding="utf-8")
    sh = stack_h.read_text(encoding="utf-8")
    sc = stack_c.read_text(encoding="utf-8")
    cmake = core_cmake.read_text(encoding="utf-8")
    ah = platform_h.read_text(encoding="utf-8")
    ac = adapter_c.read_text(encoding="utf-8")
    rt = runtime_c.read_text(encoding="utf-8")
    kconfig = kconfig_p.read_text(encoding="utf-8")
    board_dts = dts_p.read_text(encoding="utf-8")
    config = config_p.read_text(encoding="utf-8", errors="replace")
    generated_dts = generated_dts_p.read_text(encoding="utf-8", errors="replace")
    link_map = map_p.read_text(encoding="utf-8", errors="replace")

    # Frozen watchdog schema / heartbeat IDs.
    require(parse_numeric_macro(ph, "AMS_WATCHDOG_TIMEOUT_MS") == 5000,
            "watchdog timeout drifted from 5000 ms oracle")
    require(parse_numeric_macro(ph, "AMS_WATCHDOG_STARTUP_GRACE_MS") == 3000,
            "watchdog startup grace drifted from 3000 ms oracle")
    for token in (
        "AMS_WATCHDOG_HEARTBEAT_ADBMS = 0",
        "AMS_WATCHDOG_HEARTBEAT_CURRENT",
        "AMS_WATCHDOG_HEARTBEAT_TEMP",
        "AMS_WATCHDOG_HEARTBEAT_CAN",
        "AMS_WATCHDOG_HEARTBEAT_LOGGER",
        "AMS_WATCHDOG_HEARTBEAT_IMD",
        "AMS_WATCHDOG_HEARTBEAT_FAN",
        "AMS_WATCHDOG_HEARTBEAT_ESTIMATOR",
        "AMS_WATCHDOG_HEARTBEAT_VALID_MASK",
        "AMS_WATCHDOG_BLOCK_NONE = 0",
        "AMS_WATCHDOG_BLOCK_NOT_ENABLED = 1",
        "AMS_WATCHDOG_BLOCK_PANIC = 2",
        "AMS_WATCHDOG_BLOCK_STARTUP_GRACE = 3",
        "AMS_WATCHDOG_BLOCK_HEARTBEAT = 4",
        "AMS_WATCHDOG_BLOCK_ADBMS_STALE = 5",
        "AMS_WATCHDOG_BLOCK_CURRENT_STALE = 6",
        "AMS_WATCHDOG_BLOCK_TEMP_STALE = 7",
        "AMS_WATCHDOG_BLOCK_HARD_FAULT = 8",
        "AMS_WATCHDOG_BLOCK_STOP_FEED_TEST = 9",
        "AMS_WATCHDOG_BLOCK_START_FAILED = 10",
        "AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY = 11",
    ):
        require(token in ph, f"watchdog oracle schema drift: {token}")

    # Exact v2.6.27 heartbeat monitor is now a portable core, not duplicated
    # ad-hoc in the Zephyr supervisor.
    require(
        re.search(
            r"^\s*#define\s+AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS\s+"
            r"AMS_WATCHDOG_STARTUP_GRACE_MS\s*$",
            hh,
            re.MULTILINE,
        ) is not None,
        "heartbeat startup grace must alias the policy grace as one source of truth",
    )
    heartbeat_expected = {
        "AMS_WATCHDOG_HEARTBEAT_ADBMS_TIMEOUT_MS": 3000,
        "AMS_WATCHDOG_HEARTBEAT_CURRENT_TIMEOUT_MS": 200,
        "AMS_WATCHDOG_HEARTBEAT_TEMP_TIMEOUT_MS": 3000,
        "AMS_WATCHDOG_HEARTBEAT_CAN_TIMEOUT_MS": 2000,
        "AMS_WATCHDOG_HEARTBEAT_LOGGER_TIMEOUT_MS": 2000,
        "AMS_WATCHDOG_HEARTBEAT_IMD_TIMEOUT_MS": 500,
        "AMS_WATCHDOG_HEARTBEAT_FAN_TIMEOUT_MS": 1000,
        "AMS_WATCHDOG_HEARTBEAT_ESTIMATOR_TIMEOUT_MS": 500,
    }
    for macro, expected in heartbeat_expected.items():
        require(parse_numeric_macro(hh, macro) == expected,
                f"heartbeat oracle timeout drift: {macro}")
    require("watchdog/ams_watchdog_heartbeat.c" in cmake,
            "portable heartbeat monitor missing from ams_core build")
    require("now_ms - monitor->boot_ms" in hc and
            "<\n        AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS" in hc,
            "heartbeat startup grace is not wrap-safe <3000 ms")
    require("if (!startup_grace)" in hc,
            "unseen heartbeat must become stale exactly when grace ends")
    require("now_ms - monitor->last_ms[i]) > timeout_ms" in hc,
            "seen heartbeat equality boundary must remain fresh")
    require("prior_count != UINT32_MAX" in hc,
            "heartbeat count no longer saturates")
    require("return AMS_WATCHDOG_HEARTBEAT_ALL_MASK;" in hc,
            "NULL heartbeat monitor must fail closed outside oracle domain")

    # Policy: exact valid-domain oracle behavior plus defensive fail-closed
    # guards around invalid caller/mask states.
    require("input->oracle_required_mask & input->migration_evidence_mask" in pc,
            "two-mask effective watchdog evidence model missing")
    require("input->stale_mask & effective_required" in pc,
            "watchdog stale policy is not restricted to effective evidence")
    require("input->now_ms - input->boot_ms" in pc,
            "startup grace no longer uses wrap-safe unsigned subtraction")
    require("input->rtos_integrity_fault || (input->stack_critical_mask != 0U)" in pc,
            "RTOS/stack integrity no longer blocks feeding")
    require("(state == NULL) || (input == NULL)" in pc and
            "AMS_WATCHDOG_BLOCK_RTOS_INTEGRITY" in pc,
            "missing policy state/input must fail closed")
    require("~AMS_WATCHDOG_HEARTBEAT_VALID_MASK" in pc and
            ("input->oracle_required_mask | input->migration_evidence_mask |\n"
             "          input->stale_mask") in pc and
            "out-of-domain bit" in pc,
            "invalid oracle/evidence/stale heartbeat-mask bits must fail closed")
    for forbidden in (
        "voltage_fault", "temperature_fault", "current_fault", "can_fault",
        "fan_fault", "imd_fault",
    ):
        require(forbidden not in ph and forbidden not in pc,
                f"process fault leaked into watchdog starvation policy: {forbidden}")

    # Proactive stack policy: exact oracle floors/percentages; unknown query is
    # critical.  Runtime must calculate percentages against the same usable
    # Zephyr stack object queried by k_thread_stack_space_get(), not just the
    # pre-alignment source request.
    stack_expected = {
        "AMS_STACK_WARN_FLOOR_BYTES": 384,
        "AMS_STACK_WARN_PERCENT": 25,
        "AMS_STACK_CRITICAL_FLOOR_BYTES": 256,
        "AMS_STACK_CRITICAL_PERCENT": 15,
    }
    for macro, expected in stack_expected.items():
        require(parse_numeric_macro(sh, macro) == expected,
                f"stack threshold drift: {macro}")
    require("if (!query_valid)" in sc and "health.critical = true;" in sc,
            "failed stack query must be fail-closed critical")
    require("unused_bytes < health.warning_threshold_bytes" in sc and
            "unused_bytes < health.critical_threshold_bytes" in sc,
            "stack threshold equality boundary drifted")
    scan = function_block(rt, "static void runtime_scan_stack_integrity")
    require("k_thread_stack_space_get" in scan,
            "runtime no longer queries real Zephyr stack headroom")
    require("ams_stack_health_evaluate(desc->stack_size," in scan,
            "stack percentages must use usable stack_size queried by Zephyr")
    require("ams_stack_health_evaluate(desc->configured_stack_bytes," not in scan,
            "requested stack macro must not weaken percentage headroom policy")
    require("BUILD_ASSERT(AMS_THREAD_COUNT <= 16U" in rt,
            "runtime integrity masks must be width-bounded at compile time")
    require("BUILD_ASSERT(AMS_WATCHDOG_HEARTBEAT_COUNT <= 16U" in rt,
            "watchdog heartbeat masks must be width-bounded at compile time")

    # Platform adapter is mechanism-only and treats STM32 IWDG setup as an
    # irreversible ambiguity boundary.
    for state in (
        "AMS_WATCHDOG_PLATFORM_UNPREPARED",
        "AMS_WATCHDOG_PLATFORM_READY_NOT_STARTED",
        "AMS_WATCHDOG_PLATFORM_PREPARE_FAILED_RETRYABLE",
        "AMS_WATCHDOG_PLATFORM_STARTED",
        "AMS_WATCHDOG_PLATFORM_START_AMBIGUOUS_TERMINAL",
        "AMS_WATCHDOG_PLATFORM_FEED_FAILED_TERMINAL",
    ):
        require(state in ah, f"watchdog platform state missing: {state}")
    require(".max = AMS_WATCHDOG_PLATFORM_TIMEOUT_MS" in ac,
            "watchdog adapter does not install 5000 ms timeout")
    require(".flags = WDT_FLAG_RESET_SOC" in ac,
            "watchdog adapter must request SoC reset")
    require(".callback = NULL" in ac,
            "watchdog reset path must not install callback")
    start = function_block(ac, "ams_watchdog_start_result_t ams_watchdog_platform_start")
    mark_pos = start.find("START_AMBIGUOUS_TERMINAL")
    setup_pos = start.find("wdt_setup(")
    require(mark_pos >= 0 and setup_pos > mark_pos,
            "terminal ambiguity must be marked before irreversible wdt_setup")
    require("wdt_disable(" not in ac,
            "production watchdog adapter must never disable STM32 IWDG")
    require("WDT_OPT_PAUSE_HALTED_BY_DBG" not in ac,
            "validation/release watchdog must not pause under debugger")
    require("hwinfo_get_reset_cause" in ac and "RESET_WATCHDOG" in ac,
            "watchdog reset-cause evidence path missing")
    require("BUILD_ASSERT(AMS_WATCHDOG_PLATFORM_TIMEOUT_MS == AMS_WATCHDOG_TIMEOUT_MS" in rt,
            "runtime must compile-time bind platform IWDG timeout to policy timeout")

    # Production ownership scans deliberately ignore generated build C.
    production_c = list(production_c_sources(repo))
    feed_callers = [
        path.relative_to(repo).as_posix()
        for path, text in production_c
        if "ams_watchdog_platform_feed(" in text and path != adapter_c
    ]
    require(feed_callers == ["app/src/ams_threads.c"],
            f"safety supervisor must be sole platform feeder, callers={feed_callers}")
    direct_driver_callers = [
        path.relative_to(repo).as_posix()
        for path, text in production_c
        if "wdt_feed(" in text and path != adapter_c
    ]
    require(not direct_driver_callers,
            f"Zephyr wdt_feed bypasses AMS adapter: {direct_driver_callers}")
    raw_monitor_users = [
        path.relative_to(repo).as_posix()
        for path, text in production_c
        if ("ams_watchdog_heartbeat_kick(" in text or
            "ams_watchdog_heartbeat_update(" in text) and path != heartbeat_c
    ]
    require(raw_monitor_users == ["app/src/ams_threads.c"],
            f"portable heartbeat monitor bypasses runtime ownership: {raw_monitor_users}")

    hb_kick = function_block(rt, "static void watchdog_heartbeat_kick")
    lock_pos = hb_kick.find("k_spin_lock(&watchdog_heartbeat_lock)")
    kick_pos = hb_kick.find("ams_watchdog_heartbeat_kick(")
    unlock_pos = hb_kick.find("k_spin_unlock(&watchdog_heartbeat_lock")
    require(0 <= lock_pos < kick_pos < unlock_pos,
            "worker heartbeat commit must stay inside bounded spinlock")

    hb_snapshot = function_block(rt, "static struct watchdog_heartbeat_snapshot watchdog_heartbeat_snapshot_get")
    lock_pos = hb_snapshot.find("k_spin_lock(&watchdog_heartbeat_lock)")
    now_pos = hb_snapshot.find("k_uptime_get_32()")
    update_pos = hb_snapshot.find("ams_watchdog_heartbeat_update(")
    unlock_pos = hb_snapshot.find("k_spin_unlock(&watchdog_heartbeat_lock")
    require(0 <= lock_pos < now_pos < update_pos < unlock_pos,
            "heartbeat snapshot time/update must be coherent inside one spinlock")

    fan = function_block(rt, "static void fan_thread_entry")
    imd = function_block(rt, "static void imd_thread_entry")
    placeholder = function_block(rt, "static void periodic_placeholder_thread")
    fan_attempt = fan.find("ams_fan_pwm_set_percent")
    fan_kick = fan.find("watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_FAN")
    fan_complete = fan.find("runtime_publish_complete")
    require(0 <= fan_attempt < fan_kick < fan_complete,
            "FAN heartbeat must follow all actuation attempts and precede completion publication")
    imd_read = imd.find("ams_imd_capture_read_at")
    imd_fail_low = imd.find("ams_bms_ok_force_low_direct();")
    imd_kick = imd.find("watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_IMD")
    imd_complete = imd.find("runtime_publish_complete")
    require(0 <= imd_read < imd_fail_low < imd_kick < imd_complete,
            "IMD heartbeat must follow capture/publication fail-low work")
    require("watchdog_heartbeat_kick(" not in placeholder,
            "placeholder thread must never generate watchdog evidence")

    # Descriptor evidence truth must match the current migration stage.
    descriptor_bounds = {
        "current": ("[AMS_THREAD_CURRENT]", "[AMS_THREAD_ADBMS]", False),
        "adbms": ("[AMS_THREAD_ADBMS]", "[AMS_THREAD_CAN]", False),
        "can": ("[AMS_THREAD_CAN]", "[AMS_THREAD_ESTIMATOR]", False),
        "estimator": ("[AMS_THREAD_ESTIMATOR]", "[AMS_THREAD_FAN]", False),
        "fan": ("[AMS_THREAD_FAN]", "[AMS_THREAD_AIR]", True),
        "imd": ("[AMS_THREAD_IMD]", "[AMS_THREAD_DIAGNOSTICS]", True),
    }
    for name, (start_marker, end_marker, ready) in descriptor_bounds.items():
        start_i = rt.find(start_marker)
        end_i = rt.find(end_marker, start_i + 1)
        require(start_i >= 0 and end_i > start_i, f"missing {name} runtime descriptor")
        block = rt[start_i:end_i]
        expected = ".safety_evidence_ready = true" if ready else ".safety_evidence_ready = false"
        require(expected in block, f"{name} safety-evidence descriptor disagrees with Z-014 scope")

    supervisor = function_block(rt, "static void safety_supervisor_thread")
    snapshot_pos = supervisor.find("watchdog_heartbeat_snapshot_get")
    scan_pos = supervisor.find("runtime_scan_stack_integrity")
    eval_pos = supervisor.find("watchdog_evaluate_current(watchdog_now_ms,")
    feed_pos = supervisor.find("ams_watchdog_platform_feed")
    complete_pos = supervisor.find("runtime_publish_complete")
    require(0 <= snapshot_pos < scan_pos < eval_pos < feed_pos < complete_pos,
            "watchdog feed must follow coherent heartbeat+stack+policy checks")
    require(supervisor.count("watchdog_heartbeat_snapshot_get()") == 1,
            "supervisor cycle must use exactly one coherent heartbeat snapshot")
    require("watchdog_stale_mask = heartbeat_snapshot.stale_mask;" in supervisor and
            "watchdog_now_ms = heartbeat_snapshot.now_ms;" in supervisor,
            "supervisor must retain one time/stale snapshot for the feed decision")
    require("stack_query_error_mask != 0U" in supervisor and
            "ams_bms_ok_force_low_direct();" in supervisor,
            "stack-query uncertainty must force direct fail-low")
    require("action.request_start" in supervisor,
            "safety supervisor must own safe start transition")
    require("ams_watchdog_policy_record_feed(&watchdog_policy_state,\n                                                watchdog_now_ms" in supervisor,
            "successful feed evidence must use the coherent supervisor timestamp")

    start_fn = function_block(rt, "int ams_threads_start(void)")
    epoch = start_fn.find("&runtime_start_ms,")
    hb_init = start_fn.find("ams_watchdog_heartbeat_init(")
    arm = start_fn.find("ams_watchdog_platform_start();")
    first_create = start_fn.find("create_thread(")
    require(0 <= epoch < hb_init < arm < first_create,
            "boot order must remain epoch -> heartbeat init -> IWDG arm -> thread construction")

    # Capability/authority truthfulness.
    require('ams-watchdog = &iwdg;' in board_dts,
            "stable standard IWDG Devicetree alias missing")
    require('&iwdg {' in board_dts and 'status = "okay";' in board_dts,
            "IWDG device must be available to the Z-014 adapter")
    require("CONFIG_WATCHDOG=y" in config and "CONFIG_HWINFO=y" in config,
            "watchdog/HWINFO subsystem not linked")
    require(symbol_enabled(config, "CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT"),
            "watchdog adapter capability must be present")
    validation = symbol_enabled(config, "CONFIG_AMS_IWDG_VALIDATION_MODE")
    active = symbol_enabled(config, "CONFIG_AMS_CAP_WATCHDOG_ACTIVE")
    require(active == validation,
            "watchdog-active capability must exactly equal explicit validation mode")
    require(symbol_disabled(config, "CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE"),
            "Z-014 must remain partial heartbeat evidence coverage")
    require(symbol_disabled(config, "CONFIG_AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED"),
            "Z-014 must not claim physical watchdog validation")
    require(symbol_disabled(config, "CONFIG_AMS_WATCHDOG_TARGET_VALIDATED"),
            "target watchdog validation claim must remain false before physical evidence")
    require(symbol_disabled(config, "CONFIG_AMS_BMS_AUTHORITY") and
            symbol_disabled(config, "CONFIG_AMS_BALANCE_AUTHORITY"),
            "watchdog validation must never grant BMS/balancing authority")
    require(symbol_disabled(config, "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE"),
            "TEMP watchdog evidence must remain disabled until a real producer is migrated")
    require("iwdg" in generated_dts.lower(), "generated DTS lost IWDG")

    for symbol in (
        "ams_watchdog_platform_prepare",
        "ams_watchdog_platform_start",
        "ams_watchdog_platform_feed",
        "ams_watchdog_policy_evaluate",
        "ams_watchdog_heartbeat_init",
        "ams_watchdog_heartbeat_kick",
        "ams_watchdog_heartbeat_update",
        "ams_stack_health_evaluate",
    ):
        require(symbol in link_map, f"linked watchdog symbol missing: {symbol}")

    # Kconfig must continue to define the separated facts; contracts cannot
    # merely infer them from missing names.
    for symbol in (
        "AMS_CAP_WATCHDOG_ADAPTER_PRESENT",
        "AMS_CAP_WATCHDOG_ACTIVE",
        "AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE",
        "AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED",
        "AMS_WATCHDOG_TARGET_VALIDATED",
    ):
        require(f"config {symbol}" in kconfig, f"missing watchdog Kconfig fact: {symbol}")

    print("PASS: Z-014 watchdog/IWDG + heartbeat-concurrency + proactive-stack safety contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
