#!/usr/bin/env python3
"""Host regression test for the target architecture checker itself.

The target checker consumes generated DTS but most of its architecture rules are
source-only. This fixture supplies the frozen board DTS as a generated-DTS stand
in so comment parsing/direct-owner regressions are exercised without a Zephyr
workspace or target toolchain.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run_checker(repo: Path, build: Path) -> subprocess.CompletedProcess[str]:
    zephyr = build / "zephyr"
    zephyr.mkdir(parents=True, exist_ok=True)
    board = repo / "boards/drexel/der26_ams/der26_ams.dts"
    (zephyr / "zephyr.dts").write_text(board.read_text(encoding="utf-8"), encoding="utf-8")
    return subprocess.run(
        (sys.executable, str(repo / "scripts/check_architecture_contract.py"),
         str(repo), str(build)),
        cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        check=False,
    )


def copy_repo(src: Path, dst: Path) -> Path:
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(
        "build", "__pycache__", "*.pyc", "*.o", "*.exe", "*.plist"))
    return dst


def require_failed(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0:
        print(result.stdout)
        raise SystemExit(f"FAIL: architecture checker negative control survived: {label}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", type=Path)
    args = ap.parse_args()
    src = args.repo_root.resolve()

    with tempfile.TemporaryDirectory(prefix="z015_arch_checker_") as td:
        root = Path(td)

        baseline_repo = copy_repo(src, root / "baseline_repo")
        baseline = run_checker(baseline_repo, root / "baseline_build")
        if baseline.returncode != 0:
            print(baseline.stdout)
            raise SystemExit("FAIL: target architecture checker host baseline is not green")

        # Current ADC source intentionally names HAL_ADC_Start/PollForConversion
        # in oracle-provenance comments. Those names must not be classified as
        # production HAL calls.
        adc_text = (baseline_repo / "drivers/ams/current_adc_stm32.c").read_text(encoding="utf-8")
        if "HAL_ADC_Start()" not in adc_text or "HAL_ADC_PollForConversion()" not in adc_text:
            raise SystemExit("FAIL: self-test lost HAL oracle-comment fixture")

        hal_repo = copy_repo(src, root / "real_hal_repo")
        hal_path = hal_repo / "drivers/ams/current_adc_stm32.c"
        text = hal_path.read_text(encoding="utf-8")
        marker = "static atomic_t adapter_state"
        if marker not in text:
            raise SystemExit("FAIL: HAL negative-control insertion marker missing")
        text = text.replace(marker, "void z015_bad_hal_call(void) { HAL_ADC_Start(0); }\n\n" + marker, 1)
        hal_path.write_text(text, encoding="utf-8")
        require_failed(run_checker(hal_repo, root / "real_hal_build"), "real HAL call")

        direct_repo = copy_repo(src, root / "direct_owner_repo")
        leak = direct_repo / "app/src/__z015_direct_owner_negative.c"
        leak.write_text("#include <soc.h>\nvoid z015_bad_direct_owner(void) { NVIC_ClearPendingIRQ((IRQn_Type)18); }\n",
                        encoding="utf-8")
        require_failed(run_checker(direct_repo, root / "direct_owner_build"),
                       "unapproved direct MCU owner")

    print("PASS: target architecture checker host self-test")
    print("  HAL names in oracle comments: ignored")
    print("  real HAL call: rejected")
    print("  unapproved direct MCU owner: rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
