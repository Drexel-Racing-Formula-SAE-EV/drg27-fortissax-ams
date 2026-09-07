#!/usr/bin/env python3

import argparse
import hashlib
import re
from pathlib import Path
import sys

ORACLE_HASHES = {
    "imd.h": "81d8dc354098544fc8164049a831eede20f6a265bbc62a8f9076548e26c43180",
    "imd.c": "b23265087af40afb06a55d56fde018cef656c9260b4a8ce9c38d43489289f73f",
    "imd_task.h": "2fd10284eeb450567ffdfddf4927fe4a1e7c9605faf9787097ab9559e71a0b12",
    "imd_task.c": "c264f5f773d7280e5227925414736e21bc08fd3f660960ec6e37f1ba3bb3e385",
    "board.c": "5e7d8c56a451ab7f183afec06435d96a1fdac32c8a0731b8e4507e98b8d677df",
    "main.c": "ae020d461b68bce5961ea85caa96f6f1f24f485ad33befaded04d3a69d609a84",
    "stm32f7xx_hal_msp.c": "60147313c3df238de122e7089331f852960392e67f7f2c9600ab1f810ae37302",
}

PORTABLE_HASHES = {
    "ams_imd.h": "c9fcadb1b75cfb303c4e8431deae1344d1c8b2e4f6df26909835652b072bdeea",
    "ams_imd.c": "7ddab208b068b49e27a1d1e252761868874caef960a1eaef4f5809ffdba8c2d7",
    "imd_capture_zephyr.h": "df7f82524aee620136fa82f349127e8c2504c080d84cb3d93ccb6ed7a2ae8800",
    "imd_capture_zephyr.c": "ffacacbdf5391c62841239c428821195be17b8cf4f7dc696e6548628883f1677",
}


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def require(cond: bool, msg: str) -> None:
    if not cond:
        fail(msg)


def normalized_sha(path: Path) -> str:
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def block(text: str, label: str) -> str:
    start = text.find(label)
    require(start >= 0, f"missing block {label}")
    brace = text.find("{", start)
    require(brace >= 0, f"malformed block {label}")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{": depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0: return text[start:i+1]
    fail(f"unterminated block {label}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", type=Path)
    ap.add_argument("build_dir", type=Path)
    args = ap.parse_args()
    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    ph = repo / "lib/ams_core/include/ams_core/ams_imd.h"
    pc = repo / "lib/ams_core/imd/ams_imd.c"
    ah = repo / "drivers/ams/imd_capture_zephyr.h"
    ac = repo / "drivers/ams/imd_capture_zephyr.c"
    runtime = repo / "app/src/ams_threads.c"
    dts = repo / "boards/drexel/der26_ams/der26_ams.dts"
    prj = repo / "app/prj.conf"
    kconfig = repo / "app/Kconfig"
    prov = repo / "lib/ams_core/imd/ORACLE_PROVENANCE.md"
    doc = repo / "docs/migration/Z013_IMD_CAPTURE_PARITY.md"
    cfg = build / "zephyr/.config"
    gdts = build / "zephyr/zephyr.dts"
    lmap = build / "zephyr/zephyr.map"

    for path in (ph, pc, ah, ac, runtime, dts, prj, kconfig, prov, doc, cfg, gdts, lmap):
        require(path.is_file(), f"missing Z-013 artifact: {path}")

    files = {p.name: p for p in (ph, pc, ah, ac)}
    for name, expected in PORTABLE_HASHES.items():
        require(normalized_sha(files[name]) == expected,
                f"{name} drifted from reviewed Z-013 implementation")

    prov_text = prov.read_text(encoding="utf-8")
    for name, digest in ORACLE_HASHES.items():
        require(digest in prov_text, f"oracle provenance missing/drifted: {name}")

    h = ph.read_text(encoding="utf-8")
    c = pc.read_text(encoding="utf-8")
    a = ac.read_text(encoding="utf-8")
    r = runtime.read_text(encoding="utf-8")
    bd = dts.read_text(encoding="utf-8")
    cfgtxt = cfg.read_text(encoding="utf-8", errors="replace")
    g = gdts.read_text(encoding="utf-8", errors="replace")
    lm = lmap.read_text(encoding="utf-8", errors="replace")

    for token in (
        "#define AMS_IMD_CAPTURE_TIMEOUT_MS 250U",
        "AMS_IMD_SHORT_TO_CHASSIS_GROUND = 0",
        "AMS_IMD_NORMAL                  = 1",
        "AMS_IMD_UNDERVOLT               = 2",
        "AMS_IMD_SPEED_START             = 3",
        "AMS_IMD_DEVICE_ERROR            = 4",
        "AMS_IMD_GROUND_FAULT            = 5",
        "AMS_IMD_UNKNOWN                 = 0xFF",
    ):
        require(token in h, f"IMD core contract drift: {token}")

    for token in (
        "dev->capture_count != UINT32_MAX",
        "now_ms - captured_tick_ms",
        "AMS_IMD_CAPTURE_TIMEOUT_MS",
        "dev->high_count > dev->total_count",
        "0.5f + (dev->frequency_hz / 10.0f)",
        "AMS_IMD_GROUND_FAULT + 1.0f",
    ):
        require(token in c, f"IMD algorithm token missing: {token}")

    for token in (
        "#define AMS_IMD_EXPECTED_TIMER_CLOCK_HZ 108000000ULL",
        "#define AMS_IMD_PWM_CHANNEL 1U",
        "PWM_POLARITY_NORMAL",
        "PWM_CAPTURE_TYPE_BOTH",
        "PWM_CAPTURE_MODE_CONTINUOUS",
        "pwm_get_cycles_per_sec",
        "pwm_configure_capture",
        "pwm_enable_capture",
        "pulse_cycles",
        "period_cycles",
        "gpio_pin_get_dt",
        "BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_IMD_TARGET_VALIDATED)",
    ):
        require(token in (a + ah.read_text(encoding="utf-8")),
                f"IMD adapter contract drift: {token}")

    for forbidden in ("malloc(", "calloc(", "realloc(", "free(", "k_work_", "can_", "spi_", "ams_sop", "ams_soh"):
        require(forbidden not in a, f"forbidden IMD-adapter coupling: {forbidden}")

    require("AMS_RUNTIME_IMD_ENABLED 1U" in r, "real IMD runtime is not enabled")
    desc = block(r, "[AMS_THREAD_IMD]")
    require(".period_ms = AMS_PERIOD_IMD_MS" in desc, "IMD period descriptor drift")
    require(".stale_deadline_ms = AMS_STALE_IMD_MS" in desc, "IMD heartbeat timeout drift")
    require(".safety_heartbeat_required = AMS_RUNTIME_IMD_ENABLED != 0U" in desc,
            "IMD heartbeat membership drift")
    require(".safety_evidence_ready = true" in desc, "real IMD workload not safety evidence")

    wstart = r.find("static void imd_thread_entry")
    wend = r.find("static void runtime_update_stale_flags", wstart)
    worker = r[wstart:wend]
    require("ams_imd_capture_read_at" in worker, "IMD worker does not read real adapter")
    require("valid && imd_state.ok_hs && (status == AMS_IMD_NORMAL)" in worker,
            "IMD task healthy predicate drift")
    require("imd_publish_snapshot" in worker, "IMD process state not coherently published")
    fpos = worker.find("ams_bms_ok_force_low_direct();")
    hpos = worker.find("runtime_publish_complete(")
    require((fpos >= 0) and (hpos > fpos), "IMD heartbeat occurs before fail-low")
    require("next_release_ms = start_ms + thread->period_ms;" in worker,
            "IMD release no longer matches osDelayUntil(entry+100ms)")
    require("release_ms = complete_ms;" in worker,
            "IMD overrun no longer retries immediately")

    startfn = r.find("int ams_threads_start(void)")
    initpos = r.find("ams_imd_capture_init(&imd_state)", startfn)
    epoch = r.find("&runtime_start_ms,", startfn)
    require((initpos >= 0) and (epoch > initpos),
            "IMD board-style initialization must precede heartbeat epoch")

    t2 = block(bd, "&timers2")
    require('status = "okay"' in t2, "TIM2 capture disabled")
    require("interrupts = <28 5>" in t2, "TIM2 IRQ/priority drift from v2.6.27")
    require("st,prescaler = <0>" in t2, "TIM2 prescaler drift")
    require("&tim2_ch1_pa5" in t2, "IMD PWM pin is not PA5/TIM2_CH1")
    require("four-channel-capture-support" not in t2,
            "four-channel capture would remove source-equivalent reset-mode topology")
    root_imd = block(bd, "imd_capture:")
    require("<&pwm2 1 0 PWM_POLARITY_NORMAL>" in root_imd, "IMD PWM spec drift")
    require("<&gpioc 5 GPIO_ACTIVE_HIGH>" in root_imd, "IMD OK_HS PC5 polarity drift")

    require("CONFIG_PWM=y" in cfgtxt, "PWM support disabled")
    require("CONFIG_PWM_CAPTURE=y" in cfgtxt, "PWM capture support disabled")
    require("# CONFIG_AMS_BMS_AUTHORITY is not set" in cfgtxt, "BMS authority enabled")
    require("# CONFIG_AMS_BALANCE_AUTHORITY is not set" in cfgtxt, "balance authority enabled")
    require("# CONFIG_AMS_IMD_TARGET_VALIDATED is not set" in cfgtxt,
            "Z-013 falsely claims physical IMD target validation")
    require("CONFIG_HEAP_MEM_POOL_SIZE=0" in cfgtxt, "IMD migration introduced application heap")

    gt2 = block(g, "timers2:")
    require('status = "okay"' in gt2, "generated TIM2 disabled")
    require("st,prescaler = < 0x0 >" in gt2, "generated TIM2 prescaler drift")
    require("interrupts = < 0x1c 0x5 >" in gt2 or "interrupts = < 28 5 >" in gt2,
            "generated TIM2 IRQ priority is not source-equivalent 5")
    require("tim2_ch1_pa5" in g, "generated DTS missing PA5/TIM2_CH1")
    require("gpioc 0x5" in g or "&gpioc 5" in g,
            "generated DTS missing PC5 OK_HS")
    require("four-channel-capture-support" not in gt2,
            "generated TIM2 unexpectedly uses four-channel capture")

    require('status = "disabled"' in block(g, "can1:"), "CAN1 enabled during Z-013")
    require('status = "disabled"' in block(g, "spi6:"), "SPI6 enabled during Z-013")
    require('status = "disabled"' in block(g, "iwdg:"), "watchdog enabled before Z-014")

    for symbol in (
        "ams_imd_init", "ams_imd_capture_publish", "ams_imd_read_at", "ams_imd_is_ok",
        "ams_imd_capture_init", "ams_imd_capture_read_at",
    ):
        require(symbol in lm, f"linked Z-013 IMD symbol missing: {symbol}")

    print("PASS: exact v2.6.27 IMD behavior + fail-closed Zephyr capture contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
