#!/usr/bin/env python3

import argparse
from pathlib import Path
import subprocess
import sys


TARGET_CONTRACTS = (
    "check_architecture_contract.py",
    "check_board_contract.py",
    "check_authority_profile.py",
    "check_runtime_contract.py",
    "check_core_contract.py",
    "check_measurement_contract.py",
    "check_current_window_contract.py",
    "check_estimator_contract.py",
    "check_power_core_contract.py",
    "check_current_sensor_contract.py",
    "check_current_adc_contract.py",
    "check_fan_pwm_contract.py",
    "check_imd_capture_contract.py",
    "check_capability_contract.py",
    "check_safety_integrity_contract.py",
    "check_watchdog_contract.py",
    "check_adbms_spi_contract.py",
    "check_z016_link_contract.py",
)

SOURCE_ONLY_CONTRACTS = (
    "check_z014_source_hygiene.py",
    "check_z015_source_hygiene.py",
    "check_null_platform_core.py",
    "check_freertos_runtime_parity.py",
)


def run(cmd, cwd: Path) -> None:
    print("\n>>> " + " ".join(str(x) for x in cmd), flush=True)
    completed = subprocess.run(cmd, cwd=cwd, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the complete Z-015 target/source contract gate in one command."
    )
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Output manifest path (default: <build_dir>/ams_build_manifest.json)",
    )
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()
    scripts = repo / "scripts"
    python = sys.executable

    if not (build / "zephyr/zephyr.elf").is_file():
        raise SystemExit(f"missing target build: {build / 'zephyr/zephyr.elf'}")

    for script in TARGET_CONTRACTS:
        path = scripts / script
        if script in ("check_board_contract.py", "check_authority_profile.py"):
            run((python, str(path), str(build)), repo)
        else:
            run((python, str(path), str(repo), str(build)), repo)

    for script in SOURCE_ONLY_CONTRACTS:
        run((python, str(scripts / script), str(repo)), repo)

    manifest = (args.manifest.resolve() if args.manifest is not None
                else build / "ams_build_manifest.json")
    run((python, str(scripts / "build_manifest.py"), str(repo), str(build), str(manifest)), repo)

    # Whitespace is part of the release gate when the source is in a Git tree.
    probe = subprocess.run(
        ("git", "rev-parse", "--is-inside-work-tree"), cwd=repo,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
    )
    if probe.returncode == 0:
        run(("git", "diff", "--check"), repo)

    print("\nPASS: complete Z-015 target/source contract suite")
    print(f"Manifest: {manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
