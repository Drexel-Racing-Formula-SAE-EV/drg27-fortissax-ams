#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import re
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


def cache_value(build: Path, key: str) -> str:
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return ""
    pattern = re.compile(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$")
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1).strip()
    return ""


def file_api_core_audit(build: Path, core: Path) -> tuple[int, str]:
    """Return (core TU count, compile-metadata text) from CMake File API.

    Visual Studio and other multi-config generators do not emit
    compile_commands.json.  The codemodel reply still exposes the target's
    source list, include paths, definitions, and compiler fragments, which is
    sufficient for the same null-platform isolation audit.
    """
    reply = build / ".cmake" / "api" / "v1" / "reply"
    indexes = sorted(reply.glob("index-*.json"), key=lambda p: p.stat().st_mtime)
    if not indexes:
        fail("null-platform build emitted neither compile_commands.json nor CMake File API index")

    index = json.loads(indexes[-1].read_text(encoding="utf-8"))
    model_ref = index.get("reply", {}).get("codemodel-v2")
    if not isinstance(model_ref, dict) or not model_ref.get("jsonFile"):
        fail("CMake File API index missing codemodel-v2 reply")

    model = json.loads((reply / model_ref["jsonFile"]).read_text(encoding="utf-8"))
    core_root = core.resolve()
    audit_parts: list[str] = []
    source_paths: set[str] = set()

    for config in model.get("configurations", []):
        for target_ref in config.get("targets", []):
            if target_ref.get("name") != "ams_core":
                continue
            target = json.loads((reply / target_ref["jsonFile"]).read_text(encoding="utf-8"))
            source_base = Path(target.get("paths", {}).get("source", str(core_root)))
            if not source_base.is_absolute():
                source_base = (build / source_base).resolve()

            for source in target.get("sources", []):
                path = Path(source.get("path", ""))
                if not path.is_absolute():
                    path = source_base / path
                resolved = path.resolve()
                try:
                    resolved.relative_to(core_root)
                except ValueError:
                    continue
                if resolved.suffix.lower() == ".c":
                    source_paths.add(resolved.as_posix().lower())

            for group in target.get("compileGroups", []):
                for define in group.get("defines", []):
                    audit_parts.append(str(define.get("define", "")))
                for include in group.get("includes", []):
                    audit_parts.append(str(include.get("path", "")))
                for fragment in group.get("compileCommandFragments", []):
                    audit_parts.append(str(fragment.get("fragment", "")))

    if not source_paths:
        fail("CMake File API contains no ams_core translation units")

    return len(source_paths), "\n".join(audit_parts)


def compile_database_core_audit(commands_path: Path, core: Path) -> tuple[int, str]:
    commands = json.loads(commands_path.read_text(encoding="utf-8"))
    core_root = str(core.resolve()).replace("\\", "/").lower()
    core_commands: list[str] = []

    for entry in commands:
        source_path = Path(entry["file"])
        if not source_path.is_absolute():
            source_path = Path(entry.get("directory", commands_path.parent)) / source_path
        source = str(source_path.resolve()).replace("\\", "/").lower()
        if source.startswith(core_root + "/"):
            core_commands.append(entry.get("command", "") or " ".join(entry.get("arguments", [])))

    if not core_commands:
        fail("compile database contains no ams_core translation units")

    return len(core_commands), "\n".join(core_commands)


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

    # Request a generator-independent codemodel before configure.  This is the
    # audit fallback for Visual Studio/Xcode, which do not produce
    # compile_commands.json even when CMAKE_EXPORT_COMPILE_COMMANDS is enabled.
    query = build / ".cmake" / "api" / "v1" / "query"
    query.mkdir(parents=True)
    (query / "codemodel-v2").write_text("", encoding="utf-8")

    configure = [
        "cmake",
        "-S", str(project),
        "-B", str(build),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_DISABLE_FIND_PACKAGE_Zephyr=TRUE",
        "-Wno-unused-cli",
    ]
    run(configure, repo)

    # CMAKE_BUILD_TYPE is ignored by multi-config generators (notably Visual
    # Studio on Windows).  Select Release explicitly for both build and CTest.
    multi_config = bool(cache_value(build, "CMAKE_CONFIGURATION_TYPES"))
    build_cmd = ["cmake", "--build", str(build)]
    if multi_config:
        build_cmd += ["--config", "Release"]
    build_cmd += ["--parallel"]
    run(build_cmd, repo)

    test_cmd = ["ctest", "--test-dir", str(build)]
    if multi_config:
        test_cmd += ["-C", "Release"]
    test_cmd += ["--output-on-failure"]
    run(test_cmd, repo)

    commands_path = build / "compile_commands.json"
    if commands_path.is_file():
        core_tus, audit_text = compile_database_core_audit(commands_path, core)
        audit_source = "compile_commands.json"
    else:
        core_tus, audit_text = file_api_core_audit(build, core)
        audit_source = "CMake File API"

    lower = audit_text.lower()
    for token in FORBIDDEN_COMPILE_TOKENS:
        if token.lower() in lower:
            fail(f"ams_core null-platform compile path leaked forbidden token: {token}")

    print(
        "PASS: ams_core true null-platform build/test and compile-path audit "
        f"({core_tus} core TUs; {audit_source})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
