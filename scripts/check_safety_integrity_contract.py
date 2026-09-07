#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def function_block(text: str, signature: str) -> str:
    start = text.find(signature)
    require(start >= 0, f"missing safety function: {signature}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed safety function: {signature}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated safety function: {signature}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    safety_path = repo / "app" / "src" / "ams_safety.c"
    runtime_path = repo / "app" / "src" / "ams_threads.c"
    config_path = build / "zephyr" / ".config"
    map_path = build / "zephyr" / "zephyr.map"

    for path in (safety_path, runtime_path, config_path, map_path):
        require(path.is_file(), f"missing safety-contract artifact: {path}")

    safety = safety_path.read_text(encoding="utf-8")
    runtime = runtime_path.read_text(encoding="utf-8")
    config = config_path.read_text(encoding="utf-8", errors="replace")
    link_map = map_path.read_text(encoding="utf-8", errors="replace")

    # Authority is intentionally stricter than the operating FreeRTOS image at
    # this migration stage.
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in config,
            "BMS_OK assertion authority enabled during Z-013")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in config,
            "balancing authority enabled during Z-013")

    # Corresponding Zephyr integrity mechanisms for the FreeRTOS assert/stack
    # overflow policy are release invariants, not optional debug preferences.
    for token, description in (
        ("CONFIG_ASSERT=y", "kernel assertions"),
        ("CONFIG_ARM_MPU=y", "ARM MPU"),
        ("CONFIG_HW_STACK_PROTECTION=y", "hardware stack protection"),
        ("CONFIG_HEAP_MEM_POOL_SIZE=0", "zero application heap"),
    ):
        require(token in config, f"required safety configuration missing: {description}")

    for token in (
        "BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BMS_AUTHORITY)",
        "BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_BALANCE_AUTHORITY)",
        "BUILD_ASSERT(IS_ENABLED(CONFIG_ASSERT)",
        "BUILD_ASSERT(IS_ENABLED(CONFIG_ARM_MPU)",
        "BUILD_ASSERT(IS_ENABLED(CONFIG_HW_STACK_PROTECTION)",
        "BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE == 0",
    ):
        require(token in safety, f"compile-time safety invariant missing: {token}")

    direct = function_block(safety, "void ams_bms_ok_force_low_direct")
    require("RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN" in direct,
            "direct fail-low no longer owns GPIOE clock")
    require("GPIOE->BSRR = BIT(AMS_BMS_OK_PIN + 16U);" in direct,
            "direct fail-low no longer performs atomic PE0 reset")
    require("GPIOE->MODER" in direct,
            "direct fail-low no longer makes PE0 an output")
    require("__DSB();" in direct and "__ISB();" in direct,
            "direct fail-low missing DSB/ISB completion barriers")

    require("SYS_INIT(ams_bms_ok_early_init, PRE_KERNEL_1, 0);" in safety,
            "earliest application-controlled fail-low hook drifted")

    fatal = function_block(safety, "void k_sys_fatal_error_handler")
    force_pos = fatal.find("ams_bms_ok_force_low_direct();")
    halt_pos = fatal.find("k_fatal_halt(reason);")
    require((force_pos >= 0) and (halt_pos > force_pos),
            "fatal handler does not force BMS_OK low before halt")

    # Heartbeat semantics are exact points that matter to the future watchdog
    # and readiness supervisor.
    require("atomic_increment_saturating_u32" in runtime,
            "heartbeat sequence no longer saturates")
    require("(startup_age_ms >= thread->startup_grace_ms)" in runtime,
            "unseen heartbeat startup boundary drifted")
    require("snapshot->heartbeat_age_ms <" in runtime,
            "diagnostic startup-grace boundary drifted")
    require("(age_ms > thread->stale_deadline_ms)" in runtime,
            "seen-heartbeat timeout boundary drifted")

    start_fn = runtime.find("int ams_threads_start(void)")
    epoch = runtime.find("&runtime_start_ms,", start_fn)
    create = runtime.find("create_thread(", start_fn)
    supervisor = runtime.find("start_thread_if_enabled(AMS_THREAD_SAFETY);", start_fn)
    worker = runtime.find("start_thread_if_enabled(AMS_THREAD_CURRENT);", start_fn)
    require((start_fn >= 0) and (epoch > start_fn) and (create > epoch) and
            (supervisor > create) and (worker > supervisor),
            "startup order must remain epoch -> object creation -> supervisor -> workers")

    require("next_release_ms = start_ms + thread->period_ms;" in runtime,
            "real safety workload no longer reanchors from actual entry")
    require("release_ms = complete_ms;" in runtime,
            "real safety workload no longer retries immediately after overrun")

    # Z-013 has two migrated real workloads: fan and IMD. Current/ADBMS/CAN/
    # estimator remain placeholders and cannot provide safety evidence.
    fan_start = runtime.find("[AMS_THREAD_FAN]")
    air_start = runtime.find("[AMS_THREAD_AIR]")
    imd_start = runtime.find("[AMS_THREAD_IMD]")
    diag_start = runtime.find("[AMS_THREAD_DIAGNOSTICS]")
    require((fan_start >= 0) and (air_start > fan_start), "fan descriptor missing")
    require((imd_start >= 0) and (diag_start > imd_start), "IMD descriptor missing")
    fan_block = runtime[fan_start:air_start]
    imd_block = runtime[imd_start:diag_start]
    require(".safety_evidence_ready = true" in fan_block,
            "real fan workload is not marked as safety evidence")
    require(".safety_evidence_ready = true" in imd_block,
            "real IMD workload is not marked as safety evidence")
    non_real = runtime[:fan_start] + runtime[air_start:imd_start] + runtime[diag_start:]
    require(".safety_evidence_ready = true" not in non_real,
            "unmigrated placeholder is being treated as safety evidence")

    imd_worker_start = runtime.find("static void imd_thread_entry")
    imd_worker_end = runtime.find("static void runtime_update_stale_flags", imd_worker_start)
    imd_worker = runtime[imd_worker_start:imd_worker_end]
    require("ams_imd_capture_read_at" in imd_worker,
            "IMD safety workload is not using the real capture adapter")
    fail_low = imd_worker.find("ams_bms_ok_force_low_direct();")
    heartbeat = imd_worker.find("runtime_publish_complete(")
    require((fail_low >= 0) and (heartbeat > fail_low),
            "bad IMD must force BMS_OK low before liveness heartbeat")

    for symbol in (
        "ams_bms_ok_force_low_direct",
        "ams_safety_init",
        "k_sys_fatal_error_handler",
    ):
        require(symbol in link_map, f"linked safety symbol missing: {symbol}")

    for forbidden in (
        "ams_bms_ok_assert",
        "ams_bms_ok_set_high",
        "ams_bms_ok_enable",
        "ams_balance_enable_authority",
    ):
        require(forbidden not in link_map,
                f"forbidden authority symbol linked: {forbidden}")

    print("PASS: Z-013 deep fail-low/runtime-integrity safety contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
