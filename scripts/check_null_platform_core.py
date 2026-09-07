#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


FORBIDDEN_COMPILE_TOKENS = (
    "zephyr/include",
    "zephyr\\include",
    "modules/hal/stm32",
    "modules\\hal\\stm32",
    "freertos",
    "cmsis-rtos",
    "cmsis_os",
    "drivers/ams",
    "drivers\\ams",
    "include/ams_platform",
    "include\\ams_platform",
    "boards/drexel/der26_ams",
    "boards\\drexel\\der26_ams",
    "__ZEPHYR__",
    "-DKERNEL",
)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def run(cmd, cwd: Path) -> None:
    print(">>> " + " ".join(str(x) for x in cmd), flush=True)
    completed = subprocess.run(cmd, cwd=cwd, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build/test the real ams_core with no Zephyr/RTOS/HAL platform available."
    )
    parser.add_argument("repo_root", type=Path)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="Build directory (default: <repo>/build/null_platform_core)",
    )
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    project = repo / "tests" / "null_platform"
    core = repo / "lib" / "ams_core"
    build = (args.build_dir.resolve() if args.build_dir is not None
             else repo / "build" / "null_platform_core")

    for path in (project / "CMakeLists.txt", project / "null_platform_smoke.c",
                 core / "CMakeLists.txt"):
        if not path.is_file():
            fail(f"missing null-platform artifact: {path}")

    # A pristine configure is intentional: stale CMake cache/include paths must
    # never make a portability regression look green.
    if build.exists():
        shutil.rmtree(build)
    build.mkdir(parents=True)

    configure = [
        "cmake",
        "-S", str(project),
        "-B", str(build),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_DISABLE_FIND_PACKAGE_Zephyr=TRUE",
        "--no-warn-unused-cli",
    ]
    run(configure, repo)
    run(("cmake", "--build", str(build), "--parallel"), repo)
    run(("ctest", "--test-dir", str(build), "--output-on-failure"), repo)

    commands_path = build / "compile_commands.json"
    if not commands_path.is_file():
        fail("null-platform build did not emit compile_commands.json")

    commands = json.loads(commands_path.read_text(encoding="utf-8"))
    core_root = str(core.resolve()).replace("\\", "/").lower()
    core_commands = []

    for entry in commands:
        source_path = Path(entry["file"])
        if not source_path.is_absolute():
            source_path = Path(entry.get("directory", build)) / source_path
        source = str(source_path.resolve()).replace("\\", "/").lower()
        if source.startswith(core_root + "/"):
            core_commands.append(entry.get("command", "") or " ".join(entry.get("arguments", [])))

    if not core_commands:
        fail("compile database contains no ams_core translation units")

    for command in core_commands:
        lower = command.lower()
        for token in FORBIDDEN_COMPILE_TOKENS:
            if token.lower() in lower:
                fail(f"ams_core null-platform compile path leaked forbidden token: {token}")

    print(f"PASS: ams_core true null-platform build/test and compile-path audit ({len(core_commands)} core TUs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
