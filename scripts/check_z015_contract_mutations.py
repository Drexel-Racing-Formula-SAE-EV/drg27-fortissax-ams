#!/usr/bin/env python3
"""Negative controls for the Z-015 private SPI6 source/architecture contract."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(repo: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        (sys.executable, str(repo / "scripts/check_z015_source_hygiene.py"), str(repo)),
        cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)


def baseline_copy(src: Path, root: Path) -> Path:
    dst = root / "repo"
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(
        "build", "__pycache__", "*.pyc", "*.o", "*.exe", "*.plist"))
    return dst


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"FAIL: mutation token not found in {path}: {old!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def expect_fail(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0:
        print(result.stdout)
        raise SystemExit(f"FAIL: unsafe Z-015 mutation survived: {label}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("repo_root", type=Path)
    args = ap.parse_args()
    src = args.repo_root.resolve()

    with tempfile.TemporaryDirectory(prefix="z015_mutations_") as td:
        root = Path(td)
        repo = baseline_copy(src, root / "baseline")
        base = run(repo)
        if base.returncode != 0:
            print(base.stdout)
            raise SystemExit("FAIL: Z-015 mutation baseline is not green")

        cases: list[str] = []

        def mutate(name: str, rel: str, old: str, new: str) -> None:
            repo = baseline_copy(src, root / name)
            replace_once(repo / rel, old, new)
            expect_fail(run(repo), name)
            cases.append(name)

        mutate("stock_spi6_enabled", "boards/drexel/der26_ams/der26_ams.dts",
               '&spi6 {\n    /*\n     * Intentionally disabled at Z-015. No Zephyr spi_stm32 device instance,\n     * IRQ handler, async context, DMA path or automatic NSS owns SPI6.\n     * Register/clock/IRQ metadata is referenced by ams_adbms_interface.\n     */\n    status = "disabled";\n};',
               '&spi6 {\n    /*\n     * Intentionally disabled at Z-015. No Zephyr spi_stm32 device instance,\n     * IRQ handler, async context, DMA path or automatic NSS owns SPI6.\n     * Register/clock/IRQ metadata is referenced by ams_adbms_interface.\n     */\n    status = "okay";\n};')
        mutate("generic_spi_enabled", "app/prj.conf", "CONFIG_SPI=n", "CONFIG_SPI=y")
        mutate("spi_async_enabled", "app/prj.conf", "CONFIG_SPI_ASYNC=n", "CONFIG_SPI_ASYNC=y")
        mutate("spi_rtio_enabled", "app/prj.conf", "CONFIG_SPI_RTIO=n", "CONFIG_SPI_RTIO=y")
        mutate("spi_dma_enabled", "app/prj.conf", "CONFIG_SPI_STM32_DMA=n", "CONFIG_SPI_STM32_DMA=y")
        mutate("lto_enabled", "app/prj.conf", "CONFIG_LTO=n", "CONFIG_LTO=y")
        mutate("soft_nss_reintroduced", "boards/drexel/der26_ams/der26_ams.dts",
               '    status = "disabled";\n};', '    st,soft-nss;\n    status = "disabled";\n};')
        mutate("cs_b_pf4", "boards/drexel/der26_ams/der26_ams.dts",
               "cs-b-gpios = <&gpioe 4 GPIO_ACTIVE_LOW>",
               "cs-b-gpios = <&gpiof 4 GPIO_ACTIVE_LOW>")
        mutate("spi_sck_wrong_pin", "boards/drexel/der26_ams/der26_ams.dts",
               "&spi6_sck_pg13", "&spi6_sck_pa5")
        mutate("input_clock_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 108000000", "const: 54000000")
        mutate("prescaler_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 256", "const: 128")
        mutate("frequency_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 421875", "const: 843750")
        mutate("timeout_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 500", "const: 50")
        mutate("transfer_bound_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 512", "const: 1024")
        mutate("dummy_byte_drift", "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
               "const: 255", "const: 0")
        mutate("mode0_polarity", "drivers/ams/adbms_spi_stm32.c",
               "LL_SPI_SetClockPolarity(spi6, LL_SPI_POLARITY_HIGH);",
               "LL_SPI_SetClockPolarity(spi6, LL_SPI_POLARITY_LOW);")
        mutate("mode0_phase", "drivers/ams/adbms_spi_stm32.c",
               "LL_SPI_SetClockPhase(spi6, LL_SPI_PHASE_2EDGE);",
               "LL_SPI_SetClockPhase(spi6, LL_SPI_PHASE_1EDGE);")
        mutate("irq_enable", "drivers/ams/adbms_spi_stm32.c",
               "irq_disable(irq);", "irq_enable(irq);")
        mutate("pending_clear_removed", "drivers/ams/adbms_spi_stm32.c",
               "    k_irq_clear_pending(irq);", "    /* pending clear removed */")
        mutate("poll_yield_inserted", "drivers/ams/adbms_spi_stm32.c",
               "    LL_SPI_Enable(spi6);", "    k_yield();\n    LL_SPI_Enable(spi6);")
        mutate("stock_transceive_added", "drivers/ams/adbms_spi_stm32.c",
               "#include <zephyr/device.h>",
               "#include <zephyr/device.h>\n/* mutation */ void *x = (void *)spi_transceive;")
        mutate("reset_recovery_removed", "drivers/ams/adbms_spi_stm32.c",
               "    ret = reset_line_toggle_dt(&spi6_reset);",
               "    ret = 0; /* reset removed */")
        mutate("integrity_counter_removed", "drivers/ams/adbms_spi_stm32.c",
               "        atomic_inc_saturating(&platform_integrity_violation_count);",
               "        /* integrity counter removed */")
        mutate("public_raw_transfer_leak", "include/ams_platform/adbms_spi_lifecycle.h",
               "int ams_adbms_spi_platform_init(void);",
               "int ams_adbms_spi_platform_init(void);\nint ams_adbms_spi_write(int string, const void *tx, unsigned len);")

        # Direct application/private transport call.
        repo = baseline_copy(src, root / "application_direct_transport")
        p = repo / "app/src/__mutation_adbms_spi.c"
        p.write_text('#include "adbms_spi_internal.h"\nvoid bad(void){(void)ams_adbms_spi_write(0,0,0);}\n', encoding="utf-8")
        expect_fail(run(repo), "application_direct_transport")
        cases.append("application_direct_transport")

        mutate("adbms_actor_promoted", "app/Kconfig",
               "config AMS_CAP_ADBMS_ACTOR_LIVE\n    bool\n    default n",
               "config AMS_CAP_ADBMS_ACTOR_LIVE\n    bool\n    default y")
        mutate("adbms_evidence_promoted", "app/Kconfig",
               "config AMS_CAP_ADBMS_SAFETY_EVIDENCE\n    bool\n    default n",
               "config AMS_CAP_ADBMS_SAFETY_EVIDENCE\n    bool\n    default y")
        mutate("physical_validation_faked", "app/Kconfig",
               "config AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED\n    bool\n    default n",
               "config AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED\n    bool\n    default y")

        # Post-Z-015 HAL-review negative controls: the current-sense ADC must
        # remain privately polled/recoverable and output-only fan timers must
        # not keep capture IRQs enabled merely because CONFIG_PWM_CAPTURE is
        # global.
        mutate("generic_adc_enabled", "app/prj.conf", "CONFIG_ADC=n", "CONFIG_ADC=y")
        mutate("adc_async_enabled", "app/prj.conf", "CONFIG_ADC_ASYNC=n", "CONFIG_ADC_ASYNC=y")
        mutate("adc_dma_enabled", "app/prj.conf", "CONFIG_ADC_STM32_DMA=n", "CONFIG_ADC_STM32_DMA=y")
        mutate("adc1_generic_owner_enabled", "boards/drexel/der26_ams/der26_ams.dts",
               "&adc1 {\n    status = \"disabled\";",
               "&adc1 {\n    status = \"okay\";")
        mutate("adc_common_reset_drift", "boards/drexel/der26_ams/der26_ams.dts",
               "resets = <&rctl STM32_RESET(APB2, 8)>;",
               "resets = <&rctl STM32_RESET(APB2, 9)>;")
        mutate("adc_irq_enabled", "drivers/ams/current_adc_stm32.c",
               "    irq_disable(irq);", "    irq_enable(irq);")
        mutate("adc_pending_clear_removed", "drivers/ams/current_adc_stm32.c",
               "    k_irq_clear_pending(irq);", "    /* ADC pending clear removed */")
        mutate("adc_recovery_reset_removed", "drivers/ams/current_adc_stm32.c",
               "    ret = reset_line_toggle_dt(&adc_common_reset);",
               "    ret = 0; /* ADC recovery reset removed */")
        mutate("adc_timeout_made_terminal", "drivers/ams/current_adc_stm32.c",
               "            return recover_and_return(-ETIMEDOUT);",
               "            atomic_set(&adapter_state, CURRENT_ADC_STATE_FAULTED); return -ETIMEDOUT;")
        mutate("adc_timeout_boundary_drift", "drivers/ams/current_adc_stm32.c",
               "        if (elapsed_ms > CURRENT_ADC_TIMEOUT_MS) {",
               "        if (elapsed_ms >= CURRENT_ADC_TIMEOUT_MS) {")
        mutate("adc_timeout_recheck_removed", "drivers/ams/current_adc_stm32.c",
               "            if (LL_ADC_IsActiveFlag_EOCS(adc) == 0U) {",
               "            if (true) {")
        mutate("adc_success_strt_clear_removed", "drivers/ams/current_adc_stm32.c",
               "    WRITE_REG(adc->SR, ~(LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS));",
               "    LL_ADC_ClearFlag_EOCS(adc); /* stale STRT left set */")
        mutate("adc_poll_yield_inserted", "drivers/ams/current_adc_stm32.c",
               "    for (;;) {", "    for (;;) { k_yield();")
        mutate("fan_output_irq_disable_removed", "drivers/ams/fan_pwm_zephyr.c",
               "        irq_disable(irqs[i]);", "        /* fan irq disable removed */")
        mutate("fan_output_irq_clear_removed", "drivers/ams/fan_pwm_zephyr.c",
               "        k_irq_clear_pending(irqs[i]);", "        /* fan irq clear removed */")
        mutate("manifest_adc_async_lie", "scripts/build_manifest.py",
               '"completion_mechanism": "private_STM32_LL_bounded_poll"',
               '"completion_mechanism": "adc_read_async_dt+k_poll"')

        repo = baseline_copy(src, root / "adc3_generic_owner_enabled")
        with (repo / "boards/drexel/der26_ams/der26_ams.dts").open("a", encoding="utf-8") as fp:
            fp.write('\n&adc3 { status = "okay"; };\n')
        expect_fail(run(repo), "adc3_generic_owner_enabled")
        cases.append("adc3_generic_owner_enabled")

        repo = baseline_copy(src, root / "wake_api")
        p = repo / "drivers/ams/__mutation_wake.c"
        p.write_text("void adbms_wakeup(void) {}\n", encoding="utf-8")
        expect_fail(run(repo), "wake_api")
        cases.append("wake_api")

        print(f"PASS: Z-015 contract mutation suite ({len(cases)} unsafe mutations rejected)")
        for name in cases:
            print(f"  rejected: {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
