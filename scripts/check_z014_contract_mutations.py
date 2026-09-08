#!/usr/bin/env python3
"""Negative/mutation self-tests for the safety-critical Z-014 source contracts.

This intentionally edits disposable copies and proves the contract checkers
reject representative unsafe changes. It also freezes two prior checker bugs:
Windows path normalization and generated Zephyr syscall C must not create false
watchdog-feeder failures.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run_checker(repo: Path, build: Path, script: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        (sys.executable, str(repo / "scripts" / script), str(repo), str(build)),
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def run_source_checker(repo: Path, script: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        (sys.executable, str(repo / "scripts" / script), str(repo)),
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def require_pass(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        print(result.stdout)
        raise SystemExit(f"FAIL mutation harness baseline: {label}")


def require_fail(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0:
        print(result.stdout)
        raise SystemExit(f"FAIL mutation survived contract: {label}")


def write_synthetic_build(build: Path) -> None:
    zephyr = build / "zephyr"
    generated = zephyr / "include" / "generated" / "zephyr"
    generated.mkdir(parents=True, exist_ok=True)

    (zephyr / ".config").write_text(
        "\n".join(
            (
                "CONFIG_WATCHDOG=y",
                "CONFIG_HWINFO=y",
                "CONFIG_ASSERT=y",
                "CONFIG_ARM_MPU=y",
                "CONFIG_HW_STACK_PROTECTION=y",
                "CONFIG_THREAD_STACK_INFO=y",
                "CONFIG_INIT_STACKS=y",
                "CONFIG_HEAP_MEM_POOL_SIZE=0",
                "CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT=y",
                "CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT=y",
                "CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT=y",
                "CONFIG_AMS_CAP_FAN_ACTOR_LIVE=y",
                "CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE=y",
                "CONFIG_AMS_CAP_IMD_CAPTURE_ADAPTER_PRESENT=y",
                "CONFIG_AMS_CAP_IMD_ACTOR_LIVE=y",
                "CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE=y",
                "CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT=y",
            )
        ) + "\n",
        encoding="utf-8",
    )
    (zephyr / "zephyr.dts").write_text(
        '/dts-v1/;\n/ { };\niwdg: watchdog@40003000 { status = "okay"; };\n',
        encoding="utf-8",
    )
    symbols = (
        "ams_watchdog_platform_prepare",
        "ams_watchdog_platform_start",
        "ams_watchdog_platform_feed",
        "ams_watchdog_policy_evaluate",
        "ams_watchdog_heartbeat_init",
        "ams_watchdog_heartbeat_kick",
        "ams_watchdog_heartbeat_update",
        "ams_stack_health_evaluate",
        "ams_bms_ok_force_low_direct",
        "ams_bms_ok_platform_init_low",
        "ams_safety_init",
        "k_sys_fatal_error_handler",
    )
    (zephyr / "zephyr.map").write_text("\n".join(symbols) + "\n", encoding="utf-8")

    # Regression fixture: generated Zephyr dispatch legitimately contains the
    # syscall name and must not be scanned as an AMS production bypass.
    (generated / "syscall_dispatch.c").write_text(
        "int generated_only(void) { return wdt_feed(0, 0); }\n",
        encoding="utf-8",
    )


def fresh_copy(source: Path, destination: Path) -> Path:
    repo = destination / "repo"
    shutil.copytree(
        source,
        repo,
        ignore=shutil.ignore_patterns("build", "__pycache__", "*.pyc", "*.o", "*.exe", "*.plist"),
    )
    write_synthetic_build(repo / "build" / "mutation_fixture")
    return repo


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) < 1:
        raise SystemExit(f"FAIL mutation harness could not find token for {label}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    args = parser.parse_args()
    source = args.repo_root.resolve()

    with tempfile.TemporaryDirectory(prefix="z014_contract_mutations_") as tmp:
        root = Path(tmp)

        # Baseline + generated-source false-positive regression.
        repo = fresh_copy(source, root / "baseline")
        build = repo / "build" / "mutation_fixture"
        for script in (
            "check_watchdog_contract.py",
            "check_capability_contract.py",
            "check_safety_integrity_contract.py",
        ):
            require_pass(run_checker(repo, build, script), script)
        require_pass(
            run_source_checker(repo, "check_z014_source_hygiene.py"),
            "check_z014_source_hygiene.py",
        )

        cases: list[tuple[str, str]] = []

        # 1. Direct Zephyr feeder bypass under production app source.
        repo = fresh_copy(source, root / "direct_wdt")
        (repo / "app/src/__mutation_direct_wdt.c").write_text(
            "void bad(void) { (void)wdt_feed(0, 0); }\n", encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "direct wdt_feed bypass")
        cases.append(("direct wdt_feed bypass", "rejected"))

        # 2. A second AMS platform feeder.
        repo = fresh_copy(source, root / "second_feeder")
        (repo / "drivers/ams/__mutation_second_feeder.c").write_text(
            "void bad(void) { (void)ams_watchdog_platform_feed(); }\n", encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "second platform feeder")
        cases.append(("second platform feeder", "rejected"))

        # 3. Placeholder promoted to safety evidence.
        repo = fresh_copy(source, root / "placeholder_evidence")
        replace_once(repo / "app/src/ams_threads.c",
                     "[AMS_THREAD_CURRENT] = {", "[AMS_THREAD_CURRENT] = {", "current descriptor")
        text = (repo / "app/src/ams_threads.c").read_text(encoding="utf-8")
        start = text.index("[AMS_THREAD_CURRENT]")
        end = text.index("[AMS_THREAD_ADBMS]", start)
        block = text[start:end].replace(".safety_evidence_ready = false",
                                        ".safety_evidence_ready = true", 1)
        (repo / "app/src/ams_threads.c").write_text(text[:start] + block + text[end:], encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "placeholder safety evidence")
        cases.append(("placeholder safety evidence", "rejected"))

        # 4. Stack policy weakened back to requested pre-alignment bytes.
        repo = fresh_copy(source, root / "stack_size")
        replace_once(repo / "app/src/ams_threads.c",
                     "ams_stack_health_evaluate(desc->stack_size,",
                     "ams_stack_health_evaluate(desc->configured_stack_bytes,",
                     "usable stack size")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "stack percentage size weakening")
        cases.append(("stack percentage size weakening", "rejected"))

        # 5. Supervisor samples watchdog time outside the heartbeat lock.
        repo = fresh_copy(source, root / "coherent_time")
        path = repo / "app/src/ams_threads.c"
        text = path.read_text(encoding="utf-8")
        old = """    struct watchdog_heartbeat_snapshot snapshot;\n    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);\n\n    /* Capture time"""
        new = """    struct watchdog_heartbeat_snapshot snapshot;\n    snapshot.now_ms = k_uptime_get_32();\n    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);\n\n    /* Capture time"""
        if old not in text:
            raise SystemExit("FAIL mutation harness coherent-time prelude missing")
        text = text.replace(old, new, 1)
        text = text.replace("    snapshot.now_ms = k_uptime_get_32();\n", "", 1 + 0)  # removes first occurrence
        # The previous line removed the injected line; re-create a true outside-lock
        # mutation by replacing the original in-lock assignment with a comment and
        # inserting the sample immediately before the lock.
        text = path.read_text(encoding="utf-8") if False else text
        # Reconstruct deterministically from source to avoid ambiguous replace.
        src_text = (source / "app/src/ams_threads.c").read_text(encoding="utf-8")
        src_text = src_text.replace(
            "    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);\n\n    /* Capture time",
            "    snapshot.now_ms = k_uptime_get_32();\n    k_spinlock_key_t key = k_spin_lock(&watchdog_heartbeat_lock);\n\n    /* Capture time",
            1,
        )
        # remove the next in-lock assignment, leaving only the pre-lock sample
        marker = "    snapshot.now_ms = k_uptime_get_32();"
        first = src_text.find(marker)
        second = src_text.find(marker, first + len(marker))
        if second < 0:
            raise SystemExit("FAIL mutation harness could not form coherent-time mutation")
        src_text = src_text[:second] + "    /* mutated: no in-lock time sample */" + src_text[second + len(marker):]
        path.write_text(src_text, encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "heartbeat time outside lock")
        cases.append(("heartbeat time outside lock", "rejected"))

        # 6. Authority accidentally enabled in generated configuration.
        repo = fresh_copy(source, root / "authority")
        config = repo / "build/mutation_fixture/zephyr/.config"
        config.write_text(config.read_text(encoding="utf-8") + "CONFIG_AMS_BMS_AUTHORITY=y\n",
                          encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "BMS authority enabled")
        cases.append(("BMS authority enabled", "rejected"))

        # 7. Deferred TEMP evidence falsely claimed.
        repo = fresh_copy(source, root / "temp_evidence")
        config = repo / "build/mutation_fixture/zephyr/.config"
        config.write_text(config.read_text(encoding="utf-8") +
                          "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE=y\n", encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "unmigrated TEMP evidence")
        cases.append(("unmigrated TEMP evidence", "rejected"))

        # 8. Attempt to add an IWDG disable path.
        repo = fresh_copy(source, root / "disable")
        adapter = repo / "drivers/ams/watchdog_zephyr.c"
        adapter.write_text(adapter.read_text(encoding="utf-8") +
                           "\nvoid __mutation(void) { (void)wdt_disable(watchdog_dev); }\n",
                           encoding="utf-8")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "IWDG disable path")
        cases.append(("IWDG disable path", "rejected"))

        # 9. Out-of-schema heartbeat mask guard removed.
        repo = fresh_copy(source, root / "invalid_mask_guard")
        policy = repo / "lib/ams_core/watchdog/ams_watchdog_policy.c"
        replace_once(policy, "~AMS_WATCHDOG_HEARTBEAT_VALID_MASK",
                     "~UINT16_MAX", "invalid heartbeat mask guard")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "invalid heartbeat-mask guard removal")
        cases.append(("invalid heartbeat-mask guard removal", "rejected"))

        # 10. Stale-mask corruption silently masked instead of rejected.
        repo = fresh_copy(source, root / "invalid_stale_guard")
        policy = repo / "lib/ams_core/watchdog/ams_watchdog_policy.c"
        replace_once(
            policy,
            "input->oracle_required_mask | input->migration_evidence_mask |\n          input->stale_mask",
            "input->oracle_required_mask | input->migration_evidence_mask",
            "invalid stale-mask guard",
        )
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "invalid stale-mask guard removal")
        cases.append(("invalid stale-mask guard removal", "rejected"))

        # 11. Heartbeat grace duplicated instead of inheriting policy truth.
        repo = fresh_copy(source, root / "grace_duplication")
        heartbeat_h = repo / "lib/ams_core/include/ams_core/ams_watchdog_heartbeat.h"
        replace_once(heartbeat_h,
                     "#define AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS AMS_WATCHDOG_STARTUP_GRACE_MS",
                     "#define AMS_WATCHDOG_HEARTBEAT_STARTUP_GRACE_MS 3000U",
                     "heartbeat grace alias")
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "heartbeat grace duplicate source")
        cases.append(("heartbeat grace duplicate source", "rejected"))

        # 12. Platform reset timeout allowed to drift from policy timeout.
        repo = fresh_copy(source, root / "timeout_bind")
        runtime = repo / "app/src/ams_threads.c"
        replace_once(
            runtime,
            "BUILD_ASSERT(AMS_WATCHDOG_PLATFORM_TIMEOUT_MS == AMS_WATCHDOG_TIMEOUT_MS,",
            "BUILD_ASSERT(AMS_WATCHDOG_PLATFORM_TIMEOUT_MS != AMS_WATCHDOG_TIMEOUT_MS,",
            "platform/policy timeout compile-time bind",
        )
        require_fail(run_checker(repo, repo / "build/mutation_fixture", "check_watchdog_contract.py"),
                     "platform/policy timeout bind removal")
        cases.append(("platform/policy timeout bind removal", "rejected"))

        # 13. Dynamic allocation introduced into production runtime.
        repo = fresh_copy(source, root / "dynamic_alloc")
        (repo / "app/src/__mutation_heap.c").write_text(
            "void bad(void) { (void)k_malloc(8); }\n", encoding="utf-8")
        require_fail(run_source_checker(repo, "check_z014_source_hygiene.py"),
                     "dynamic allocation in production runtime")
        cases.append(("dynamic allocation in production runtime", "rejected"))

        # 14. Raw MCU register ownership leaks into app orchestration.
        repo = fresh_copy(source, root / "raw_register")
        (repo / "app/src/__mutation_raw_register.c").write_text(
            "void bad(void) { GPIOE->BSRR = 1; }\n", encoding="utf-8")
        require_fail(run_source_checker(repo, "check_z014_source_hygiene.py"),
                     "raw MCU register in app")
        cases.append(("raw MCU register in app", "rejected"))

        # 15. Positive BMS_OK authority surface appears before its migration gate.
        repo = fresh_copy(source, root / "bms_authority_api")
        header = repo / "include/ams_platform/bms_ok.h"
        header.write_text(
            header.read_text(encoding="utf-8") +
            "\nint ams_bms_ok_assert_high(void);\n", encoding="utf-8")
        require_fail(run_source_checker(repo, "check_z014_source_hygiene.py"),
                     "positive BMS_OK authority API")
        cases.append(("positive BMS_OK authority API", "rejected"))

        # 16. Frozen SoP/SoH/fuse source identity drifts during an unrelated
        # watchdog-stage edit. The source-only gate must catch it without a
        # Zephyr build directory.
        repo = fresh_copy(source, root / "power_core_drift")
        sop = repo / "lib/ams_core/sop/ams_sop.c"
        sop.write_text(sop.read_text(encoding="utf-8") +
                       "\n/* __mutation_power_core_drift */\n", encoding="utf-8")
        require_fail(run_source_checker(repo, "check_power_core_contract.py"),
                     "frozen power-core source drift")
        cases.append(("frozen power-core source drift", "rejected"))

        # 17. Fatal halt moved ahead of the independent board-owned fail-low
        # primitive. A fatal path must never depend on later code executing to
        # remove BMS_OK.
        repo = fresh_copy(source, root / "fatal_order")
        safety = repo / "app/src/ams_safety.c"
        replace_once(
            safety,
            "    ams_bms_ok_force_low_direct();\n    atomic_set(&ams_panic_latched, 1);\n\n    k_fatal_halt(reason);",
            "    k_fatal_halt(reason);\n    ams_bms_ok_force_low_direct();\n    atomic_set(&ams_panic_latched, 1);",
            "fatal fail-low ordering",
        )
        require_fail(run_source_checker(repo, "check_freertos_runtime_parity.py"),
                     "fatal halt before direct fail-low")
        cases.append(("fatal halt before direct fail-low", "rejected"))

        # 18. Runtime object/thread construction failure allowed to return from
        # main after the IWDG may already have been armed. The only safe Z-014
        # behavior is to enter the fatal fail-low path.
        repo = fresh_copy(source, root / "runtime_start_return")
        main_c = repo / "app/src/main.c"
        text = main_c.read_text(encoding="utf-8")
        start = text.find("ret = ams_threads_start();")
        panic = text.find("k_panic();", start)
        if start < 0 or panic < 0:
            raise SystemExit("FAIL mutation harness runtime-start panic precondition missing")
        text = text[:panic] + "return ret;" + text[panic + len("k_panic();"):]
        main_c.write_text(text, encoding="utf-8")
        require_fail(run_source_checker(repo, "check_z014_source_hygiene.py"),
                     "runtime-start failure returns instead of panicking")
        cases.append(("runtime-start failure returns instead of panicking", "rejected"))

    print(f"PASS: Z-014 contract mutation suite ({len(cases)} unsafe mutations rejected)")
    for label, result in cases:
        print(f"  {result}: {label}")
    print("  pass: generated Zephyr syscall C ignored as non-production source")
    print("  pass: hidden default-n capabilities may be absent from .config")
    return 0


if __name__ == "__main__":
    sys.exit(main())
