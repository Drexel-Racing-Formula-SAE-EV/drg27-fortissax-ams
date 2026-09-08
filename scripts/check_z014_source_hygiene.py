#!/usr/bin/env python3
"""Source-only architectural/safety boundary gate for the current Z-014/Z-015 safety baseline.

This checker intentionally needs no Zephyr build directory.  It complements the
build-aware contracts by catching source-boundary regressions in the same host
run that exercises the portable SIL suites.
"""

from __future__ import annotations

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


def source_files(root: Path, subdirs: tuple[str, ...]) -> list[Path]:
    files: list[Path] = []
    for subdir in subdirs:
        base = root / subdir
        if not base.exists():
            continue
        files.extend(path for path in base.rglob("*") if path.suffix in {".c", ".h"})
    return sorted(files)


def relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def occurrences(root: Path, files: list[Path], pattern: re.Pattern[str]) -> list[str]:
    hits: list[str] = []
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if pattern.search(line):
                hits.append(f"{relative(root, path)}:{line_no}")
    return hits


def kconfig_block(text: str, symbol: str) -> str:
    marker = f"config {symbol}"
    start = text.find(marker)
    require(start >= 0, f"missing migration capability Kconfig symbol: {symbol}")
    next_config = text.find("\nconfig ", start + len(marker))
    next_endmenu = text.find("\nendmenu", start + len(marker))
    candidates = [x for x in (next_config, next_endmenu) if x >= 0]
    end = min(candidates) if candidates else len(text)
    return text[start:end]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    args = parser.parse_args()
    repo = args.repo_root.resolve()

    prod_files = source_files(repo, ("app", "boards", "drivers", "include", "lib", "zephyr"))
    require(prod_files, "no production source files found")

    # Portable core must not import either RTOS, MCU HAL, or the platform
    # adapter surface.  This is stricter than merely succeeding in a build
    # where those headers happen not to be on the include path.
    core_files = source_files(repo, ("lib/ams_core",))
    forbidden_core_include = re.compile(
        r"^\s*#\s*include\s*[<\"](?:zephyr/|FreeRTOS|cmsis_os|stm32|ams_platform/)"
    )
    core_hits = occurrences(repo, core_files, forbidden_core_include)
    require(not core_hits,
            "portable ams_core imported RTOS/HAL/platform headers: " + str(core_hits))

    # Dynamic allocation/workqueue execution is intentionally absent from the
    # current safety runtime.  Static thread/storage ownership is part of the
    # migration safety model, not a style preference.
    runtime_files = source_files(repo, ("app", "drivers", "lib"))
    dynamic_pattern = re.compile(
        r"\b(?:malloc|calloc|realloc|free|k_malloc|k_calloc|k_realloc|k_free|"
        r"k_work_submit|k_work_schedule|k_work_reschedule)\s*\("
    )
    dynamic_hits = occurrences(repo, runtime_files, dynamic_pattern)
    require(not dynamic_hits,
            "dynamic allocation/system-workqueue dependency introduced: " + str(dynamic_hits))

    # Raw STM32 register ownership is exceptional and belongs only to the
    # board-level emergency fail-low primitive.  Normal app/drivers must stay
    # behind Zephyr/platform abstractions.
    raw_reg_pattern = re.compile(
        r"\b(?:RCC|GPIO[A-K]|TIM\d+|ADC\d+|IWDG|CAN\d+|SPI\d+)->"
    )
    raw_hits = occurrences(repo, prod_files, raw_reg_pattern)
    allowed_raw = "boards/drexel/der26_ams/ams_fail_low_stm32.c"
    bad_raw = [hit for hit in raw_hits if not hit.startswith(allowed_raw + ":")]
    require(not bad_raw,
            "raw MCU register access escaped board fail-low primitive: " + str(bad_raw))
    require(any(hit.startswith(allowed_raw + ":") for hit in raw_hits),
            "board emergency fail-low primitive no longer has direct-register ownership")

    # App orchestration must not grow direct device-driver/Devicetree access;
    # hardware belongs in drivers/boards and portable policy in ams_core.
    app_files = source_files(repo, ("app/src",))
    app_driver_include = re.compile(r"^\s*#\s*include\s*<zephyr/drivers/")
    app_dt_pattern = re.compile(r"\b(?:DT_NODELABEL|DT_ALIAS|GPIO_DT_SPEC|PWM_DT_SPEC|ADC_DT_SPEC)\b")
    require(not occurrences(repo, app_files, app_driver_include),
            "app orchestration imported a Zephyr hardware driver directly")
    require(not occurrences(repo, app_files, app_dt_pattern),
            "app orchestration acquired direct Devicetree hardware ownership")

    # BMS_OK public authority remains structurally impossible in the no-authority migration baseline: the
    # platform surface provides only normal init-low and unconditional fail-low.
    bms_header = (repo / "include/ams_platform/bms_ok.h").read_text(encoding="utf-8")
    fail_low_header = (repo / "include/ams_platform/fail_low.h").read_text(encoding="utf-8")
    require("ams_bms_ok_platform_init_low" in bms_header,
            "BMS_OK init-low platform API missing")
    require("ams_bms_ok_force_low_direct" in fail_low_header,
            "direct BMS_OK fail-low API missing")
    forbidden_authority = re.compile(
        r"\bams_bms_ok_[A-Za-z0-9_]*(?:high|assert|enable|set)\b", re.IGNORECASE
    )
    authority_hits = occurrences(
        repo,
        source_files(repo, ("include/ams_platform", "app", "drivers", "boards")),
        forbidden_authority,
    )
    # init_low contains no authority verb above; any future positive-operation
    # API/function should therefore be visible here.
    require(not authority_hits,
            "positive BMS_OK authority API/source introduced: " + str(authority_hits))

    # The direct board implementation must never contain the obvious PE0 set
    # operation while this stage is no-authority.
    fail_low_source = (repo / allowed_raw).read_text(encoding="utf-8")
    require("GPIOE->BSRR = BIT(AMS_BMS_OK_PIN);" not in fail_low_source,
            "board fail-low primitive contains a PE0 set/high operation")

    # IWDG ownership is one-way and single-owner.  Application code may call
    # only the AMS platform feed from ams_threads.c; only the adapter may call
    # Zephyr wdt_feed; no production source may call wdt_disable.
    wdt_feed_hits = occurrences(repo, prod_files, re.compile(r"\bwdt_feed\s*\("))
    require(
        len(wdt_feed_hits) == 1 and
        wdt_feed_hits[0].startswith("drivers/ams/watchdog_zephyr.c:"),
        "Zephyr wdt_feed must exist exactly once in watchdog adapter: " + str(wdt_feed_hits),
    )
    require(not occurrences(repo, prod_files, re.compile(r"\bwdt_disable\s*\(")),
            "watchdog disable path introduced")

    platform_feed_hits = occurrences(
        repo, prod_files, re.compile(r"\bams_watchdog_platform_feed\s*\(")
    )
    # Header declaration + adapter definition + sole application caller are
    # expected.  Reject any extra production caller.
    caller_hits = [h for h in platform_feed_hits if h.startswith("app/")]
    require(len(caller_hits) == 1 and caller_hits[0].startswith("app/src/ams_threads.c:"),
            "safety supervisor must remain sole application platform feeder: " + str(caller_hits))

    # Production heartbeat kicks must be owned by the runtime integration;
    # portable core provides the mechanism but no other actor may fabricate
    # evidence directly.
    hb_call_hits = occurrences(
        repo,
        source_files(repo, ("app", "boards", "drivers")),
        re.compile(r"\bams_watchdog_heartbeat_kick\s*\("),
    )
    require(hb_call_hits and all(h.startswith("app/src/ams_threads.c:") for h in hb_call_hits),
            "watchdog heartbeat evidence escaped runtime owner: " + str(hb_call_hits))

    # Startup failures that can occur after fail-low initialization -- and, in
    # validation mode, after crossing the irreversible IWDG start boundary --
    # must enter Zephyr's fatal path rather than return to a partially-started
    # application.  The fatal override independently forces BMS_OK low before
    # halting.
    main_source = (repo / "app/src/main.c").read_text(encoding="utf-8")
    safety_init = main_source.find("ret = ams_safety_init();")
    safety_fail = main_source.find("if (ret != 0)", safety_init)
    safety_panic = main_source.find("k_panic();", safety_fail)
    core_check = main_source.find("ret = ams_core_contract_check();", safety_init)
    require(0 <= safety_init < safety_fail < safety_panic < core_check,
            "safety-init failure must enter fatal fail-low path before later startup")

    runtime_start = main_source.find("ret = ams_threads_start();")
    runtime_fail = main_source.find("if (ret != 0)", runtime_start)
    runtime_panic = main_source.find("k_panic();", runtime_fail)
    post_start = main_source.find("ams_imd_capture_started()", runtime_start)
    require(0 <= runtime_start < runtime_fail < runtime_panic < post_start,
            "runtime-start failure must panic before any partially-started continuation")

    # No configuration fragment in the shipped source may grant vehicle
    # authority or claim physical validation.  Validation/starvation configs
    # may arm IWDG but still must leave evidence claims false.
    for conf in sorted((repo / "app").glob("*.conf")):
        text = conf.read_text(encoding="utf-8")
        for forbidden in (
            "CONFIG_AMS_BMS_AUTHORITY=y",
            "CONFIG_AMS_BALANCE_AUTHORITY=y",
            "CONFIG_AMS_WATCHDOG_TARGET_VALIDATED=y",
            "CONFIG_AMS_IMD_TARGET_VALIDATED=y",
            "CONFIG_AMS_FAN_TARGET_VALIDATED=y",
        ):
            require(forbidden not in text,
                    f"unsafe authority/validation claim in {relative(repo, conf)}: {forbidden}")

    # Migration capabilities remain hidden facts. Deferred actors/evidence
    # cannot accidentally default to true at the source level.
    kconfig = (repo / "app/Kconfig").read_text(encoding="utf-8")
    deferred = (
        "AMS_CAP_CURRENT_ACTOR_LIVE",
        "AMS_CAP_CURRENT_SAFETY_EVIDENCE",
        "AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED",
        "AMS_CAP_ADBMS_ACTOR_LIVE",
        "AMS_CAP_ADBMS_SAFETY_EVIDENCE",
        "AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE",
        "AMS_CAP_CAN_ADAPTER_PRESENT",
        "AMS_CAP_CAN_ACTOR_LIVE",
        "AMS_CAP_CAN_SAFETY_EVIDENCE",
        "AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE",
    )
    for symbol in deferred:
        block = kconfig_block(kconfig, symbol)
        require("default y" not in block,
                f"deferred migration capability defaults true: {symbol}")
        lines = block.splitlines()
        require(not any(line.strip().startswith(('bool "', 'tristate "')) for line in lines[1:]),
                f"migration capability became user-selectable: {symbol}")

    promoted = kconfig_block(kconfig, "AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT")
    require("default y" in promoted,
            "Z-015 ADBMS SPI adapter presence capability is not promoted")
    require(not any(line.strip().startswith(('bool "', 'tristate "'))
                    for line in promoted.splitlines()[1:]),
            "ADBMS SPI adapter presence capability became user-selectable")

    # No balancing platform surface exists yet; later migration must add it as
    # an explicit stage instead of smuggling authority through a generic API.
    platform_names = [p.name.lower() for p in (repo / "include/ams_platform").glob("*.h")]
    require(not any("balanc" in name for name in platform_names),
            "balancing platform authority surface introduced during no-authority migration")

    print("PASS: Z-014/Z-015 source-only architecture/safety hygiene contract")
    print(f"  scanned production C/H files: {len(prod_files)}")
    print("  portable core: no RTOS/HAL/platform imports")
    print("  runtime: static/no-workqueue; app hardware ownership remains abstracted")
    print("  authority: no BMS_OK assert path, no balancing surface")
    print("  watchdog: sole feeder, no disable path, runtime-owned heartbeat evidence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
