#!/usr/bin/env python3
"""Host self-test for the Z-015 final-ELF zero-transfer-caller gate.

This does not replace the target check. It proves that the stdlib ELF parser and
function-section-GC assumption used by check_adbms_spi_contract.py distinguish a
lifecycle-only linked image from one that has real raw transport callers.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_contract(repo: Path):
    path = repo / "scripts/check_adbms_spi_contract.py"
    spec = importlib.util.spec_from_file_location("z015_adbms_contract", path)
    require(spec is not None and spec.loader is not None, "could not load target contract module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compile_fixture(repo: Path, cc: str, source: Path, output: Path) -> None:
    cmd = [
        cc,
        "-std=c11",
        "-O2",
        "-ffunction-sections",
        "-fdata-sections",
        "-Wl,--gc-sections",
        f"-I{repo / 'drivers/ams'}",
        f"-I{repo / 'tests/unit/adbms_spi/fake_zephyr'}",
        f"-I{repo / 'include'}",
        str(source),
        str(repo / "tests/unit/adbms_spi/fake_zephyr/fake_zephyr_spi.c"),
        str(repo / "drivers/ams/adbms_spi_engine.c"),
        str(repo / "drivers/ams/adbms_spi_stm32.c"),
        "-o",
        str(output),
    ]
    cp = subprocess.run(cmd, cwd=repo, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, check=False)
    if cp.returncode != 0:
        print(cp.stdout)
        fail(f"fixture compile failed: {source.name}")


def main() -> int:
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
    require(bool(cc), "GCC/cc is required for the Z-015 ELF caller-gate self-test")
    contract = load_contract(repo)

    lifecycle_source = r'''#include <ams_platform/adbms_spi_lifecycle.h>
#include "fake_zephyr_spi.h"
int main(void) {
    fake_spi_reset();
    if (ams_adbms_spi_platform_init() != 0) return 2;
    ams_adbms_spi_platform_status_t s = ams_adbms_spi_platform_status();
    return s.state == AMS_ADBMS_SPI_PLATFORM_READY ? 0 : 3;
}
'''
    caller_source = r'''#include <ams_platform/adbms_spi_lifecycle.h>
#include "adbms_spi_internal.h"
#include "fake_zephyr_spi.h"
#include <stdint.h>
int main(void) {
    uint8_t tx = 0x5aU, rx = 0U;
    fake_spi_reset();
    if (ams_adbms_spi_platform_init() != 0) return 2;
    if (ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A, &tx, 1U) != AMS_ADBMS_SPI_RESULT_OK) return 3;
    return ams_adbms_spi_write_read(AMS_ADBMS_SPI_STRING_B, &tx, 1U, &rx, 1U) == AMS_ADBMS_SPI_RESULT_OK ? 0 : 4;
}
'''

    with tempfile.TemporaryDirectory(prefix="z015_elf_gate_") as td:
        tmp = Path(td)
        lifecycle_c = tmp / "lifecycle_only.c"
        caller_c = tmp / "with_callers.c"
        lifecycle_elf = tmp / "lifecycle_only.elf"
        caller_elf = tmp / "with_callers.elf"
        lifecycle_c.write_text(lifecycle_source, encoding="utf-8")
        caller_c.write_text(caller_source, encoding="utf-8")
        compile_fixture(repo, cc, lifecycle_c, lifecycle_elf)
        compile_fixture(repo, cc, caller_c, caller_elf)

        lifecycle_symbols = contract.elf_defined_symbols(lifecycle_elf)
        caller_symbols = contract.elf_defined_symbols(caller_elf)

        for symbol in ("ams_adbms_spi_platform_init", "ams_adbms_spi_platform_status"):
            require(symbol in lifecycle_symbols,
                    f"lifecycle-only fixture lost required linked symbol: {symbol}")
        for symbol in ("ams_adbms_spi_write", "ams_adbms_spi_write_read"):
            require(symbol not in lifecycle_symbols,
                    f"function-section GC did not remove uncalled raw entrypoint: {symbol}")
            require(symbol in caller_symbols,
                    f"ELF gate cannot observe a real raw transport caller: {symbol}")

    print("PASS: Z-015 final-ELF caller-gate self-test")
    print("  lifecycle-only image: raw transfer entrypoints absent")
    print("  caller image: raw transfer entrypoints present/detectable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
