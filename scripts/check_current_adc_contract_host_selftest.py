#!/usr/bin/env python3
"""Host regressions for current ADC comments and generated Kconfig handling.

The synthetic DTS/map/config below exercise the complete checker; they are not
STM32 target-build evidence.
"""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import sys
import subprocess
import tempfile


def check_generated_config_fixtures(repo: Path, checker: Path) -> None:
    base = """# CONFIG_ADC is not set
CONFIG_AMS_CURRENT_ADC_PRIVATE_BACKEND=y
CONFIG_USE_STM32_LL_ADC=y
# CONFIG_AMS_BMS_AUTHORITY is not set
# CONFIG_AMS_BALANCE_AUTHORITY is not set
CONFIG_HEAP_MEM_POOL_SIZE=0
"""
    generated_dts = """ams_current_sense: current-sense {
    compatible = "drexel,ams-current-sense";
    status = "okay";
    high-channel = <3>;
    low-channel = <10>;
    adc-input-clock-hz = <108000000>;
    adc-prescaler = <6>;
    adc-clock-hz = <18000000>;
    resolution-bits = <12>;
    acquisition-ticks = <480>;
    conversion-timeout-ms = <5>;
};
adc1: adc@0 { status = "disabled"; };
adc2: adc@1 { status = "disabled"; };
adc3: adc@2 { status = "disabled"; };
"""
    cases = [
        ("omitted-dependent-symbols", base, None),
        ("explicit-not-set", base + "# CONFIG_ADC_ASYNC is not set\n"
         "# CONFIG_ADC_STM32_DMA is not set\n", None),
        ("explicit-n", base + "CONFIG_ADC_ASYNC=n\nCONFIG_ADC_STM32_DMA=n\n", None),
        ("parent-explicit-n", base.replace("# CONFIG_ADC is not set", "CONFIG_ADC=n"), None),
        ("unrelated-symbol-prefix", base + "CONFIG_ADC_ASYNC_OTHER=y\n", None),
        ("missing-parent", base.replace("# CONFIG_ADC is not set\n", ""),
         "generic CONFIG_ADC must remain disabled"),
    ]
    for symbol, diagnostic in (
        ("CONFIG_ADC", "generic CONFIG_ADC must remain disabled"),
        ("CONFIG_ADC_ASYNC", "CONFIG_ADC_ASYNC must remain disabled"),
        ("CONFIG_ADC_STM32_DMA", "ADC DMA must remain disabled"),
    ):
        for value in ("y", "m", "invalid"):
            cases.append((f"{symbol}-{value}", base + f"{symbol}={value}\n", diagnostic))
        cases.append((f"{symbol}-contradictory", base +
                      f"# {symbol} is not set\n{symbol}=y\n{symbol}=n\n", diagnostic))

    with tempfile.TemporaryDirectory(prefix="ams-adc-contract-") as temp:
        build = Path(temp)
        zephyr = build / "zephyr"
        zephyr.mkdir()
        (zephyr / "zephyr.dts").write_text(generated_dts, encoding="utf-8")
        (zephyr / "zephyr.map").write_text(
            "ams_current_adc_init\nams_current_adc_read_pair\nams_current_adc_is_faulted\n",
            encoding="utf-8",
        )
        for name, config, diagnostic in cases:
            (zephyr / ".config").write_text(config, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(checker), str(repo), str(build)],
                capture_output=True, text=True, check=False,
            )
            if diagnostic is None:
                valid = result.returncode == 0 and "PASS:" in result.stdout
            else:
                valid = result.returncode == 1 and f"FAIL: {diagnostic}" in result.stdout
            if not valid:
                fail(f"generated-config fixture {name}: {result.stdout}{result.stderr}")
    print(f"PASS: current ADC complete-checker synthetic config fixtures ({len(cases)} cases)")


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", nargs="?", type=Path, default=Path("."))
    args = ap.parse_args()
    repo = args.repo_root.resolve()
    checker = repo / "scripts" / "check_current_adc_contract.py"

    spec = importlib.util.spec_from_file_location("current_adc_contract", checker)
    if spec is None or spec.loader is None:
        fail("unable to import current ADC contract checker")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    sample = r'''
/* Oracle provenance: adc_context, adc_read_async, k_poll_signal,
 * k_yield(), k_sleep(), and HAL_ADC_Start() are intentionally NOT used. */
// Another rationale mention: irq_enable(18); adc_context
static int safe_path(void) {
    return 0;
}
'''
    code = mod.strip_c_comments(sample)
    for token in ("adc_context", "adc_read_async", "k_poll_signal", "k_yield(",
                  "k_sleep(", "irq_enable(", "HAL_ADC_Start("):
        if token in code:
            fail(f"comment-only token survived executable-code filter: {token}")

    injected = sample + "\nstatic void bad(void) { adc_context(); }\n"
    if "adc_context" not in mod.strip_c_comments(injected):
        fail("real executable adc_context token was hidden by comment filter")

    injected_yield = sample + "\nstatic void bad2(void) { k_yield(); }\n"
    if "k_yield(" not in mod.strip_c_comments(injected_yield):
        fail("real executable k_yield token was hidden by comment filter")

    check_generated_config_fixtures(repo, checker)
    print("PASS: current ADC contract executable-code/comment filtering self-test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
