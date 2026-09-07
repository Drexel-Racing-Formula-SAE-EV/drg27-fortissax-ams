#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path
import sys

ORACLE_FAN_TASK_SHA256 = "050e15dafbe95433a254f6f994cfb262dc388c32098a0a7849f64044ec782f63"
ORACLE_FANS_SHA256 = "4793d9df6130d43e0f079b295b478b99a5452e6e407e76f1101b2392072aaf02"
ORACLE_HELPER_CANON_SHA256 = "035abe2a419462eb4771523a5a74c4bfc5fc5b72a558b63470d8a4f48c8492a2"
ORACLE_POLICY_CANON_SHA256 = "3f464250367b7e3c29a2d0b89303a1e22910a16da7972edd7dcb672bd32dcca1"


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def extract_function(text: str, signature: str) -> str:
    start = text.find(signature)
    require(start >= 0, f"missing function: {signature}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed function: {signature}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated function: {signature}")


def canonical_policy(text: str, candidate: bool) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    if candidate:
        replacements = (
            ("ams_fan_control_input_t", "app_data_t"),
            ("ams_fan_percent_from_temp", "fan_percent_from_temp"),
            ("AMS_FAN_RAMP_START_C", "TEMP_FAN_RAMP_START_C"),
            ("AMS_FAN_MAX_C", "TEMP_FAN_MAX_C"),
            ("AMS_FAN_MIN_COMMAND_PERCENT", "FAN_MIN_COMMAND_PERCENT"),
            ("AMS_FAN_CHARGE_WARM_PERCENT", "FAN_CHARGE_WARM_PERCENT"),
            ("AMS_FAN_OFF_HYSTERESIS_C", "FAN_OFF_HYSTERESIS_C"),
            ("AMS_FAN_STATE_CHARGE", "STATE_CHARGE"),
            ("AMS_FAN_CONTROL_REASON_", "FAN_CONTROL_REASON_"),
        )
        for old, new in replacements:
            text = text.replace(old, new)
    text = re.sub(r"^\s*static\s+", "", text)
    return re.sub(r"\s+", "", text)


def get_block(text: str, label: str) -> str:
    start = text.find(label)
    require(start >= 0, f"missing DTS block {label}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed DTS block {label}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated DTS block {label}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", type=Path)
    ap.add_argument("build_dir", type=Path)
    args = ap.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    core_h = repo / "lib/ams_core/include/ams_core/ams_fan_control.h"
    core_c = repo / "lib/ams_core/fan/ams_fan_control.c"
    driver_h = repo / "drivers/ams/fan_pwm_zephyr.h"
    driver_c = repo / "drivers/ams/fan_pwm_zephyr.c"
    runtime_c = repo / "app/src/ams_threads.c"
    main_c = repo / "app/src/main.c"
    board_dts = repo / "boards/drexel/der26_ams/der26_ams.dts"
    provenance = repo / "lib/ams_core/fan/ORACLE_PROVENANCE.md"
    config = build / "zephyr/.config"
    generated_dts = build / "zephyr/zephyr.dts"
    link_map = build / "zephyr/zephyr.map"

    for path in (core_h, core_c, driver_h, driver_c, runtime_c, main_c,
                 board_dts, provenance, config, generated_dts, link_map):
        require(path.is_file(), f"missing Z-012 artifact: {path}")

    h = core_h.read_text(encoding="utf-8")
    c = core_c.read_text(encoding="utf-8")
    d = driver_c.read_text(encoding="utf-8")
    dh = driver_h.read_text(encoding="utf-8")
    r = runtime_c.read_text(encoding="utf-8")
    m = main_c.read_text(encoding="utf-8")
    bd = board_dts.read_text(encoding="utf-8")
    cfg = config.read_text(encoding="utf-8", errors="replace")
    gd = generated_dts.read_text(encoding="utf-8", errors="replace")
    lm = link_map.read_text(encoding="utf-8", errors="replace")
    prov = provenance.read_text(encoding="utf-8")

    # Exact thermal policy constants from v2.6.27.
    for token in (
        "#define AMS_FAN_RAMP_START_C        35.0f",
        "#define AMS_FAN_MAX_C               50.0f",
        "#define AMS_FAN_MIN_COMMAND_PERCENT 25.0f",
        "#define AMS_FAN_CHARGE_WARM_PERCENT 35.0f",
        "#define AMS_FAN_OFF_HYSTERESIS_C     3.0f",
        "#define AMS_FAN_STATE_CHARGE 2U",
    ):
        require(token in h, f"fan policy constant drift: {token}")

    # The copied policy bodies are canonical-equivalent to the frozen oracle;
    # only type/function/constant namespaces are adapted.
    helper = canonical_policy(extract_function(c, "static float fan_temp_for_control"), True)
    policy = canonical_policy(extract_function(c, "float ams_fan_percent_from_temp"), True)
    require(hashlib.sha256(helper.encode()).hexdigest() == ORACLE_HELPER_CANON_SHA256,
            "fan temperature authority helper diverged from v2.6.27 oracle")
    require(hashlib.sha256(policy.encode()).hexdigest() == ORACLE_POLICY_CANON_SHA256,
            "fan thermal policy diverged from v2.6.27 oracle")

    # Freeze the exact oracle provenance alongside the implementation.
    require(ORACLE_FAN_TASK_SHA256 in prov,
            "fan_task.c oracle provenance missing/drifted")
    require(ORACLE_FANS_SHA256 in prov,
            "fans.c oracle provenance missing/drifted")
    require(ORACLE_HELPER_CANON_SHA256 in prov,
            "fan helper canonical provenance missing/drifted")
    require(ORACLE_POLICY_CANON_SHA256 in prov,
            "fan policy canonical provenance missing/drifted")

    # Exact HAL CCR behavior translated to Zephyr cycles.
    for token in (
        "#define AMS_FAN_ZONE_COUNT 6U",
        "#define AMS_FAN_PWM_PERIOD_CYCLES 3361U",
        "#define AMS_FAN_PWM_MAX_COMPARE_CYCLES 3360U",
        "#define AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ 108000000ULL",
    ):
        require(token in dh, f"fan PWM contract drift: {token}")

    mappings = (
        "DEVICE_DT_GET(DT_NODELABEL(pwm3)), 2U",
        "DEVICE_DT_GET(DT_NODELABEL(pwm3)), 4U",
        "DEVICE_DT_GET(DT_NODELABEL(pwm4)), 3U",
        "DEVICE_DT_GET(DT_NODELABEL(pwm4)), 4U",
        "DEVICE_DT_GET(DT_NODELABEL(pwm5)), 1U",
        "DEVICE_DT_GET(DT_NODELABEL(pwm5)), 2U",
    )
    for mapping in mappings:
        require(mapping in d, f"fan hardware mapping missing: {mapping}")

    require("PWM_POLARITY_NORMAL" in d, "fan polarity must remain active high")
    require("pwm_set_cycles" in d, "fan adapter must use cycle-exact PWM API")
    require("pwm_get_cycles_per_sec" in d, "fan adapter must validate timer clock")
    require("cycles_per_sec != AMS_FAN_PWM_EXPECTED_TIMER_CLOCK_HZ" in d,
            "fan adapter does not fail closed on timer-clock drift")
    require("startup_fail_mask = ams_fan_pwm_force_all_off();" in d,
            "fan startup does not explicitly command all zones off")

    # No fan-driver coupling to unrelated policy/transport or dynamic work.
    for forbidden in (
        "malloc(", "calloc(", "realloc(", "free(", "k_work_",
        "can_", "spi_", "ams_sop", "ams_soh", "BMS_OK",
    ):
        require(forbidden not in d, f"forbidden fan-adapter coupling: {forbidden}")

    # Runtime parity: one real 5 Hz fan worker, all six attempts, fault count,
    # heartbeat after actuation, and v2.6.27 overrun re-anchor semantics.
    require("static void fan_thread_entry" in r, "real fan worker missing")
    require("zone < AMS_FAN_ZONE_COUNT" in r, "fan worker does not attempt all six zones")
    set_pos = r.find("ams_fan_pwm_set_percent(zone, percent)")
    heartbeat_pos = r.find("runtime_publish_complete(thread, complete_ms, exec_us)", set_pos)
    require((set_pos >= 0) and (heartbeat_pos > set_pos),
            "fan heartbeat is published before real actuation attempt")
    require("fan_increment_fail_count();" in r, "fan channel failures not counted")
    require("AMS_FAN_CONTROL_REASON_DRIVER_FAULT" in r,
            "fan driver failure does not override diagnostic reason")
    require("next_release_ms = start_ms + thread->period_ms;" in r,
            "fan release is not re-anchored to iteration entry like v2.6.27")
    require("release_ms = complete_ms;" in r,
            "fan overrun does not retry without an extra skipped period")
    fan_desc = r[r.find("[AMS_THREAD_FAN]"):r.find("[AMS_THREAD_AIR]")]
    require(".safety_evidence_ready = true" in fan_desc,
            "real fan worker not eligible as software-liveness evidence")

    # Missing temperature input in Z-012 must fail conservatively to max cooling.
    require("input.temp_valid = false;" in r, "temperature absence not explicit")
    require("input.temp_read_fault = true;" in r, "temperature absence not fail-max")
    require("input.temp_usable_sensor_count = 0U;" in r,
            "temperature absence fabricates usable sensors")

    # Platform init failure is fatal; per-channel startup failures are reported
    # but do not panic, matching HAL timer init vs fan_init failure distinction.
    init_pos = m.find("ret = ams_fan_pwm_init();")
    panic_pos = m.find("k_panic();", init_pos)
    mask_pos = m.find("ams_fan_pwm_startup_fail_mask()", panic_pos)
    require((init_pos >= 0) and (panic_pos > init_pos) and (mask_pos > panic_pos),
            "fan startup fatal/soft failure separation drifted")

    # Source DTS contract.
    for label, pins in (
        ("&timers3", "&tim3_ch2_pa7 &tim3_ch4_pb1"),
        ("&timers4", "&tim4_ch3_pd14 &tim4_ch4_pd15"),
        ("&timers5", "&tim5_ch1_pa0 &tim5_ch2_pa1"),
    ):
        block = get_block(bd, label)
        require('status = "okay"' in block, f"{label} disabled")
        require("st,prescaler = <0>" in block, f"{label} prescaler drift")
        require(pins in block, f"{label} pin mapping drift")

    # Target build contract.
    require("CONFIG_PWM=y" in cfg, "Zephyr PWM support disabled")
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in cfg,
            "BMS authority must remain disabled during fan migration")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in cfg,
            "balance authority must remain disabled during fan migration")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in cfg,
            "fan migration must remain application-heap-free")

    for label in ("timers3:", "timers4:", "timers5:"):
        block = get_block(gd, label)
        require('status = "okay"' in block, f"generated {label} disabled")
        require("st,prescaler = < 0x0 >" in block, f"generated {label} prescaler drift")

    for pin in (
        "tim3_ch2_pa7", "tim3_ch4_pb1", "tim4_ch3_pd14",
        "tim4_ch4_pd15", "tim5_ch1_pa0", "tim5_ch2_pa1",
    ):
        require(pin in gd, f"generated DTS missing fan pin {pin}")

    # CAN and ADBMS SPI remain outside the basic-driver migration. TIM2 is
    # allowed to become active in the subsequent Z-013 IMD capture stage; the
    # dedicated IMD contract owns its exact semantics.
    require('status = "disabled"' in get_block(gd, "can1:"), "CAN1 enabled during basic-driver migration")
    require('status = "disabled"' in get_block(gd, "spi6:"), "SPI6 enabled during basic-driver migration")
    timer2 = get_block(gd, "timers2:")
    if "CONFIG_PWM_CAPTURE=y" in cfg:
        require('status = "okay"' in timer2,
                "TIM2 must be enabled when the later IMD capture stage is active")
    else:
        require('status = "disabled"' in timer2,
                "TIM2 unexpectedly enabled before IMD capture migration")

    for symbol in (
        "ams_fan_percent_from_temp",
        "ams_fan_control_reason_str",
        "ams_fan_pwm_init",
        "ams_fan_pwm_set_percent",
        "ams_fan_pwm_force_all_off",
    ):
        require(symbol in lm, f"linked Z-012 fan symbol missing: {symbol}")

    print("PASS: exact v2.6.27 fan policy + fail-closed Zephyr PWM contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
