#!/usr/bin/env python3
"""Canonical deep host/SIL gate for the current Z-014 migration boundary.

This runner intentionally does NOT build/flash a target and does NOT advance
migration scope.  It exercises only portable/currently-present Z-014 logic,
adapters and source contracts.  Historically expensive SoP/SoH/fuse campaigns
are deliberately excluded because Z-014 changes watchdog/integration safety,
not those frozen algorithms.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from typing import Sequence


UNIT_SUITES = (
    "watchdog",
    "safety_fatal",
    "bms_ok",
    "current_path",
    "fan",
    "imd",
    "measurement_store",
    "current_window",
    "estimator",
)

SYSTEM_SUITES = ("z014_safety_sil",)
SANITIZER_UNIT_SUITES = UNIT_SUITES
ANALYZER_UNIT_SUITES = UNIT_SUITES


class GateFailure(RuntimeError):
    pass


def command_string(cmd: Sequence[str]) -> str:
    return " ".join(str(part) for part in cmd)


def find_make() -> str:
    for candidate in ("make", "gmake"):
        path = shutil.which(candidate)
        if path:
            return path
    raise GateFailure("GNU make/gmake is required for the Z-014 host SIL gate")


def run_stage(
    name: str,
    cmd: Sequence[str],
    cwd: Path,
    records: list[dict[str, object]],
    env: dict[str, str] | None = None,
) -> None:
    print(f"\n=== {name} ===", flush=True)
    print(">>> " + command_string(cmd), flush=True)
    started = time.monotonic()
    completed = subprocess.run(list(cmd), cwd=cwd, env=env, check=False)
    elapsed = time.monotonic() - started
    records.append(
        {
            "name": name,
            "command": list(cmd),
            "cwd": str(cwd),
            "returncode": completed.returncode,
            "seconds": round(elapsed, 3),
        }
    )
    if completed.returncode != 0:
        raise GateFailure(f"{name} failed with exit code {completed.returncode}")


def run_make_dir(
    make: str,
    repo: Path,
    suite_dir: Path,
    stage_name: str,
    targets: Sequence[str],
    records: list[dict[str, object]],
    env: dict[str, str] | None = None,
) -> None:
    if not (suite_dir / "Makefile").is_file():
        raise GateFailure(f"missing Makefile for host suite: {stage_name}")
    run_stage(
        f"{stage_name}: {' '.join(targets)}",
        [make, "-C", str(suite_dir), *targets],
        repo,
        records,
        env,
    )


def run_make(
    make: str,
    repo: Path,
    suite: str,
    targets: Sequence[str],
    records: list[dict[str, object]],
    env: dict[str, str] | None = None,
) -> None:
    run_make_dir(make, repo, repo / "tests" / "unit" / suite, suite,
                 targets, records, env)


def run_system_make(
    make: str,
    repo: Path,
    suite: str,
    targets: Sequence[str],
    records: list[dict[str, object]],
    env: dict[str, str] | None = None,
) -> None:
    run_make_dir(make, repo, repo / "tests" / "system" / suite,
                 f"system/{suite}", targets, records, env)


def python_scripts(repo: Path) -> list[Path]:
    return sorted((repo / "scripts").glob("*.py"))


def write_report(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run the current-scope Z-014 host/SIL safety gate without target "
            "builds, hardware tests, or later migration stages."
        )
    )
    parser.add_argument("repo_root", nargs="?", type=Path, default=Path("."))
    parser.add_argument(
        "--report",
        type=Path,
        default=None,
        help="JSON report path (default: <repo>/build/z014_host_validation_report.json)",
    )
    parser.add_argument(
        "--require-clang",
        action="store_true",
        help="Fail instead of recording a skip if clang is unavailable.",
    )
    parser.add_argument(
        "--tsan",
        action="store_true",
        help="Also run optional ThreadSanitizer watchdog-concurrency evidence.",
    )
    parser.add_argument(
        "--no-sanitizers",
        action="store_true",
        help="Skip ASan/UBSan (intended only for constrained hosts, not closeout).",
    )
    parser.add_argument(
        "--no-analyzers",
        action="store_true",
        help="Skip GCC/Clang static analyzers (intended only for constrained hosts).",
    )
    parser.add_argument(
        "--keep-products",
        action="store_true",
        help="Keep host test binaries/analyzer objects after a successful run.",
    )
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    report = (args.report.resolve() if args.report is not None
              else repo / "build" / "z014_host_validation_report.json")
    records: list[dict[str, object]] = []
    skipped: list[str] = []
    started_all = time.monotonic()
    success = False

    required = (
        repo / "lib" / "ams_core" / "watchdog" / "ams_watchdog_policy.c",
        repo / "lib" / "ams_core" / "watchdog" / "ams_watchdog_heartbeat.c",
        repo / "app" / "src" / "ams_threads.c",
        repo / "scripts" / "check_z014_contract_mutations.py",
    )
    for path in required:
        if not path.is_file():
            print(f"FAIL: missing Z-014 artifact: {path}", file=sys.stderr)
            return 2

    try:
        make = find_make()
        clang = shutil.which("clang")
        if clang is None and args.require_clang:
            raise GateFailure("clang is required by --require-clang but was not found")

        print("Z-014 canonical host/SIL validation")
        print(f"repo: {repo}")
        print("scope: watchdog/IWDG integration + already-present portable/adapters only")
        print("excluded: target build/flash, physical IWDG, Z-015+, long SoP/SoH/fuse campaigns")

        # Script syntax is a safety gate too: target contract failures must not
        # be hidden behind a malformed checker.
        scripts = python_scripts(repo)
        run_stage(
            "Python contract/checker syntax",
            [sys.executable, "-m", "py_compile", *[str(p) for p in scripts]],
            repo,
            records,
        )

        # Source-only architecture/oracle gates. The null-platform gate proves
        # ams_core still builds with no Zephyr/FreeRTOS/HAL available.
        run_stage(
            "FreeRTOS v2.6.27 runtime/safety source parity",
            [sys.executable, str(repo / "scripts/check_freertos_runtime_parity.py"), str(repo)],
            repo,
            records,
        )
        run_stage(
            "Z-014 source-only architecture/safety hygiene",
            [sys.executable, str(repo / "scripts/check_z014_source_hygiene.py"), str(repo)],
            repo,
            records,
        )
        run_stage(
            "Frozen SoP/SoH/fuse exact portable-source identity",
            [sys.executable, str(repo / "scripts/check_power_core_contract.py"), str(repo)],
            repo,
            records,
        )
        run_stage(
            "ams_core true null-platform isolation",
            [sys.executable, str(repo / "scripts/check_null_platform_core.py"), str(repo)],
            repo,
            records,
        )

        # Directed + differential + production-adapter + concurrency tests.
        for suite in UNIT_SUITES:
            targets = ["clean", "test"]
            if suite in {"watchdog", "safety_fatal"}:
                targets.append("strict")
            run_make(make, repo, suite, targets, records)

        # FreeRTOS-style integrated current-boundary SIL.  This composes the
        # portable heartbeat/policy/stack mechanisms under randomized actor and
        # supervisor ordering while keeping all future authority absent.
        for suite in SYSTEM_SUITES:
            run_system_make(make, repo, suite, ["clean", "test", "strict"], records)

        if not args.no_sanitizers:
            for suite in SANITIZER_UNIT_SUITES:
                run_make(make, repo, suite, ["asan", "ubsan"], records)
            for suite in SYSTEM_SUITES:
                run_system_make(make, repo, suite, ["asan", "ubsan"], records)
        else:
            skipped.append("ASan/UBSan")

        if args.tsan:
            run_make(make, repo, "watchdog", ["tsan"], records)

        if not args.no_analyzers:
            for suite in ANALYZER_UNIT_SUITES:
                run_make(make, repo, suite, ["analyze"], records)
            for suite in SYSTEM_SUITES:
                run_system_make(make, repo, suite, ["analyze"], records)
            if clang is not None:
                clang_env = os.environ.copy()
                clang_env["CLANG"] = clang
                for suite in ANALYZER_UNIT_SUITES:
                    run_make(make, repo, suite, ["clang-analyze"], records, clang_env)
                for suite in SYSTEM_SUITES:
                    run_system_make(make, repo, suite, ["clang-analyze"], records, clang_env)
            else:
                skipped.append("Clang static analyzer (clang unavailable)")
                print("SKIP: Clang static analyzer (clang unavailable)")
        else:
            skipped.append("GCC/Clang static analyzers")

        # Mutate the safety contracts themselves. A green checker is useful
        # only if known dangerous edits make it red.
        run_stage(
            "Z-014 safety-contract mutation/negative controls",
            [sys.executable, str(repo / "scripts/check_z014_contract_mutations.py"), str(repo)],
            repo,
            records,
        )

        success = True
    except GateFailure as exc:
        print(f"\nFAIL: {exc}", file=sys.stderr, flush=True)
    finally:
        elapsed_all = time.monotonic() - started_all
        payload: dict[str, object] = {
            "schema": "der27-ams-z014-host-validation-v2",
            "success": success,
            "repo": str(repo),
            "scope": "Z-014 host/source/SIL only",
            "target_build_performed": False,
            "hardware_validation_performed": False,
            "later_migration_stage_performed": False,
            "extended_sop_soh_fuse_campaigns_performed": False,
            "frozen_power_source_identity_checked": any(
                str(stage.get("name", "")) ==
                "Frozen SoP/SoH/fuse exact portable-source identity"
                and int(stage.get("returncode", 1)) == 0
                for stage in records
            ),
            "integrated_z014_system_sil_performed": any(
                str(stage.get("name", "")).startswith("system/z014_safety_sil:")
                and int(stage.get("returncode", 1)) == 0
                for stage in records
            ),
            "thread_sanitizer_requested": bool(args.tsan),
            "thread_sanitizer_performed": any(
                str(stage.get("name", "")) == "watchdog: tsan"
                and int(stage.get("returncode", 1)) == 0
                for stage in records
            ),
            "skipped": skipped,
            "seconds": round(elapsed_all, 3),
            "stages": records,
        }
        try:
            write_report(report, payload)
            print(f"Report: {report}")
        except OSError as exc:
            print(f"WARNING: could not write report: {exc}", file=sys.stderr)

    if not success:
        return 1

    if not args.keep_products:
        make = find_make()
        cleanup_records: list[dict[str, object]] = []
        for suite in UNIT_SUITES:
            try:
                run_make(make, repo, suite, ["clean"], cleanup_records)
            except GateFailure as exc:
                print(f"WARNING: post-success cleanup failed: {exc}", file=sys.stderr)
        for suite in SYSTEM_SUITES:
            try:
                run_system_make(make, repo, suite, ["clean"], cleanup_records)
            except GateFailure as exc:
                print(f"WARNING: post-success cleanup failed: {exc}", file=sys.stderr)

    print("\nPASS: complete Z-014 deep host/source/SIL validation")
    if skipped:
        print("Skipped evidence: " + "; ".join(skipped))
    else:
        print("Skipped evidence: none")
    print("No target build, hardware test, authority enablement, or Z-015 work was performed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
