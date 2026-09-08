#!/usr/bin/env python3
"""Source-only gate for audited Z-015 private HAL ownership and safety boundaries."""

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


def block(text: str, marker: str) -> str:
    # Require the marker at the start of a source line so references in comments
    # or phandle properties cannot be mistaken for the actual node block.
    match = re.search(rf"(?m)^\s*{re.escape(marker)}\s*\{{", text)
    require(match is not None, f"missing block marker: {marker}")
    start = match.start()
    brace = text.find("{", start)
    require(brace >= 0, f"malformed block marker: {marker}")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated block: {marker}")


def files_under(repo: Path, roots: tuple[str, ...]) -> list[Path]:
    result: list[Path] = []
    for root in roots:
        base = repo / root
        if base.exists():
            result.extend(p for p in base.rglob("*") if p.is_file() and p.suffix in {".c", ".h"})
    return sorted(result)


def occurrences(repo: Path, files: list[Path], regex: re.Pattern[str]) -> list[str]:
    hits: list[str] = []
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            if regex.search(line):
                hits.append(f"{path.relative_to(repo).as_posix()}:{line_no}")
    return hits


def kconfig_block(text: str, symbol: str) -> str:
    marker = f"config {symbol}"
    start = text.find(marker)
    require(start >= 0, f"missing Kconfig symbol: {symbol}")
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

    required = (
        "drivers/ams/adbms_spi_engine.c",
        "drivers/ams/adbms_spi_engine.h",
        "drivers/ams/adbms_spi_internal.h",
        "drivers/ams/adbms_spi_stm32.c",
        "include/ams_platform/adbms_spi_lifecycle.h",
        "tests/unit/adbms_spi/Makefile",
        "docs/migration/Z015_SPI_TRANSPORT_ORACLE.md",
        "docs/migration/Z015_STM32_SPI_DRIVER_AUDIT.md",
        "docs/migration/Z015_ADBMS_CALLER_INVENTORY.md",
        "drivers/ams/current_adc_stm32.c",
        "tests/unit/current_adc/current_adc_adapter_test.c",
    )
    for rel in required:
        require((repo / rel).is_file(), f"missing Z-015 artifact: {rel}")

    west = (repo / "west.yml").read_text(encoding="utf-8")
    require(re.search(r"^\s*revision:\s*v4\.4\.0\s*$", west, re.MULTILINE) is not None,
            "Z-015 audited driver assumptions require Zephyr v4.4.0 pin")

    binding = (repo / "dts/bindings/ams/drexel,ams-adbms-interface.yaml").read_text(encoding="utf-8")
    for token in (
        "pinctrl-device.yaml", "reset-device.yaml", "spi-controller:",
        "spi-input-clock-hz:", "const: 108000000", "spi-prescaler:", "const: 256",
        "spi-frequency-hz:", "const: 421875", "spi-timeout-ms:", "const: 500",
        "max-transfer-bytes:", "const: 512", "read-dummy-byte:", "const: 255",
    ):
        require(token in binding, f"ADBMS typed binding lost frozen contract: {token}")

    board = (repo / "boards/drexel/der26_ams/der26_ams.dts").read_text(encoding="utf-8")
    adbms = block(board, "ams_adbms_interface: ams-adbms-interface")
    for token in (
        'compatible = "drexel,ams-adbms-interface"',
        "spi-controller = <&spi6>",
        "&spi6_sck_pg13", "&spi6_miso_pg12", "&spi6_mosi_pg14",
        "cs-a-gpios = <&gpioe 2 GPIO_ACTIVE_LOW>",
        "cs-b-gpios = <&gpioe 4 GPIO_ACTIVE_LOW>",
        "resets = <&rctl STM32_RESET(APB2, 21)>",
        "spi-input-clock-hz = <108000000>", "spi-prescaler = <256>",
        "spi-frequency-hz = <421875>", "spi-timeout-ms = <500>",
        "max-transfer-bytes = <512>", "read-dummy-byte = <255>",
        'status = "okay"',
    ):
        require(token in adbms, f"ADBMS board contract missing: {token}")

    spi6 = block(board, "&spi6")
    require('status = "disabled"' in spi6,
            "stock Zephyr SPI6 device must remain disabled at Z-015")
    require("pinctrl-0" not in spi6 and "cs-gpios" not in spi6 and "st,soft-nss" not in spi6,
            "disabled stock SPI6 node must not retain driver-owned pin/CS/NSS configuration")
    require("st,soft-nss" not in board,
            "st,soft-nss is meaningless with private SPI6 ownership and must stay absent")

    prj = (repo / "app/prj.conf").read_text(encoding="utf-8")
    for token in ("CONFIG_SPI=n", "CONFIG_SPI_ASYNC=n", "CONFIG_SPI_RTIO=n", "CONFIG_SPI_STM32_DMA=n", "CONFIG_LTO=n"):
        require(token in prj, f"private SPI6 configuration missing: {token}")
    for token in ("CONFIG_ADC=n", "CONFIG_ADC_ASYNC=n", "CONFIG_ADC_STM32_DMA=n"):
        require(token in prj, f"private current-ADC configuration missing: {token}")

    kconfig = (repo / "app/Kconfig").read_text(encoding="utf-8")
    private = kconfig_block(kconfig, "AMS_ADBMS_SPI_PRIVATE_BACKEND")
    require("default y" in private and "select USE_STM32_LL_SPI" in private and "select RESET" in private,
            "private backend must structurally select LL SPI + reset support")
    current_private = kconfig_block(kconfig, "AMS_CURRENT_ADC_PRIVATE_BACKEND")
    require("default y" in current_private and "select USE_STM32_LL_ADC" in current_private
            and "select RESET" in current_private,
            "private current ADC backend must structurally select LL ADC + reset support")
    present = kconfig_block(kconfig, "AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT")
    require("default y" in present, "Z-015 adapter-presence capability not promoted")
    for symbol in ("AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED", "AMS_CAP_ADBMS_ACTOR_LIVE",
                   "AMS_CAP_ADBMS_SAFETY_EVIDENCE", "AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE"):
        kb = kconfig_block(kconfig, symbol)
        require("default y" not in kb, f"Z-015 deferred capability defaults true: {symbol}")

    cmake = (repo / "drivers/ams/CMakeLists.txt").read_text(encoding="utf-8")
    require("adbms_spi_engine.c" in cmake and "adbms_spi_stm32.c" in cmake,
            "Z-015 private transport is not part of ams_platform target")

    engine = (repo / "drivers/ams/adbms_spi_engine.c").read_text(encoding="utf-8")
    engine_h = (repo / "drivers/ams/adbms_spi_engine.h").read_text(encoding="utf-8")
    require("#include <zephyr/" not in engine and "#include <stm32" not in engine.lower(),
            "transaction engine must remain host/SIL portable inside drivers/ams")
    require("#include <zephyr/" not in engine_h and "#include <stm32" not in engine_h.lower(),
            "transaction engine header must remain host/SIL portable")
    require("(uint32_t)(now_ms - start_ms) >= timeout_ms" in engine,
            "single wrap-safe absolute timeout predicate missing")
    require("read_dummy_byte" in engine and "max_transfer_bytes" in engine,
            "wire dummy/bounds policy missing from production engine")
    require("recover_after_failure" in engine and "set_cs_active" in engine,
            "engine failure path no longer restores CS/recovery ordering")
    require("intentionally have no scheduler yield/sleep hook" in engine and
            "continuous manual-CS" in engine and "normal preemptible Zephyr thread" in engine,
            "bounded polling no-yield/preemptibility rationale is no longer explicit")

    target = (repo / "drivers/ams/adbms_spi_stm32.c").read_text(encoding="utf-8")
    forbidden_target = (
        "<zephyr/drivers/spi.h>", "spi_transceive",
        "k_poll_signal", "IRQ_CONNECT", "irq_enable(", "k_sleep(", "k_yield(",
        "irq_lock(", "k_sched_lock(", "spi_dma_", "CONFIG_SPI_STM32_DMA", "st,soft-nss",
    )
    for token in forbidden_target:
        require(token not in target, f"private bounded backend contains forbidden stock/async path: {token}")
    for token in (
        "clock_control_get_rate", "108000000U", "LL_SPI_BAUDRATEPRESCALER_DIV256",
        "LL_SPI_POLARITY_HIGH", "LL_SPI_PHASE_2EDGE", "LL_SPI_DATAWIDTH_8BIT",
        "LL_SPI_MSB_FIRST", "LL_SPI_FULL_DUPLEX", "LL_SPI_NSS_SOFT",
        "LL_SPI_DisableIT_TXE", "LL_SPI_DisableIT_RXNE", "LL_SPI_DisableIT_ERR",
        "irq_disable(", "k_irq_clear_pending(", "reset_line_toggle_dt",
        "force_both_cs_inactive", "spi6_contract_readback_valid",
    ):
        require(token in target, f"private SPI6 safety mechanism missing: {token}")
    require(target.count("k_irq_clear_pending(") >= 1 and
            target.count("disable_and_clear_spi6_irq();") >= 4,
            "SPI6 pending IRQ must be cleared at init/recovery boundaries")
    require("LL_SPI_SetClockPolarity(spi6, LL_SPI_POLARITY_HIGH);" in target,
            "SPI6 programming no longer explicitly sets CPOL high")
    require("LL_SPI_SetClockPhase(spi6, LL_SPI_PHASE_2EDGE);" in target,
            "SPI6 programming no longer explicitly sets CPHA second edge")
    require("LL_SPI_SetBaudRatePrescaler(spi6, LL_SPI_BAUDRATEPRESCALER_DIV256);" in target,
            "SPI6 programming no longer explicitly sets /256")
    require(target.count("reset_line_toggle_dt(&spi6_reset)") >= 2,
            "SPI6 reset must exist in both startup preparation and transport recovery")
    require("platform_integrity_violation_count" in target and
            "atomic_inc_saturating(&platform_integrity_violation_count);" in target,
            "illegal/reentrant transfer attempts are not durably counted")
    cas_start = target.find("if (!atomic_cas(&platform_state")
    read_start = target.find("    if (read) {", cas_start)
    require(cas_start >= 0 and read_start > cas_start and
            "atomic_inc_saturating(&platform_integrity_violation_count);" in target[cas_start:read_start],
            "single-owner CAS failure does not record an integrity violation before return")

    # Post-Z-015 HAL review: current ADC uses the same ownership lesson as
    # SPI. Generic adc_stm32 async context/IRQ/DMA are structurally absent; a
    # private bounded poll can recover a transient timeout by resetting the
    # common ADC block, with ADC3 frozen disabled because ADCRST is shared.
    manifest_builder = (repo / "scripts/build_manifest.py").read_text(encoding="utf-8")
    for token in (
        '"completion_mechanism": "private_STM32_LL_bounded_poll"',
        '"completion_timeout_semantics": "HAL_F7_elapsed_gt_5ms_with_EOC_recheck"',
        '"timeout_is_terminal_by_default": False',
        '"current_adc_generic_zephyr_driver_owned": False',
        '"fan_output_timer_capture_irqs_disabled": True',
    ):
        require(token in manifest_builder, f"build manifest does not report hardened platform truth: {token}")
    for stale in (
        '"completion_mechanism": "adc_read_async_dt+k_poll"',
        '"timeout_recovery": "latched_fault_reboot_only"',
    ):
        require(stale not in manifest_builder, f"stale pre-hardening build-manifest claim survived: {stale}")

    current = (repo / "drivers/ams/current_adc_stm32.c").read_text(encoding="utf-8")
    for token in (
        "BUILD_ASSERT(!IS_ENABLED(CONFIG_ADC)",
        "BUILD_ASSERT(IS_ENABLED(CONFIG_USE_STM32_LL_ADC)",
        "DT_IRQN(CURRENT_ADC_HIGH_NODE) == 18",
        "irq_disable(irq);", "k_irq_clear_pending(irq);",
        "reset_line_toggle_dt(&adc_common_reset)",
        "LL_ADC_CLOCK_SYNC_PCLK_DIV6", "LL_ADC_RESOLUTION_12B",
        "LL_ADC_SAMPLINGTIME_480CYCLES", "CURRENT_ADC_TIMEOUT_MS",
        "WRITE_REG(adc->SR, ~(LL_ADC_FLAG_STRT | LL_ADC_FLAG_EOCS))",
        "elapsed_ms > CURRENT_ADC_TIMEOUT_MS",
        "if (LL_ADC_IsActiveFlag_EOCS(adc) == 0U)",
        "return recover_and_return(-ETIMEDOUT)",
        "adc_contract_readback_valid()", "integrity_violation_count",
    ):
        require(token in current, f"private current ADC hardening missing: {token}")
    current_code = re.sub(r"/\*.*?\*/|//[^\n]*", "", current, flags=re.DOTALL)
    for token in (
        "<zephyr/drivers/adc.h>", "adc_read_async", "k_poll(",
        "k_poll_signal", "irq_enable(", "IRQ_CONNECT(", "k_yield(", "k_sleep(",
        "k_sched_lock(", "irq_lock(",
    ):
        require(token not in current_code, f"current ADC reintroduced unsafe async/blocking ownership: {token}")
    require(current.count("disable_and_clear_adc_irq();") >= 4,
            "current ADC must quiesce/clear shared ADC IRQ across init and recovery")
    recovery_start = current.find("static int reset_and_reconfigure_adc")
    recovery_end = current.find("static int recover_and_return", recovery_start)
    require(0 <= recovery_start < recovery_end,
            "current ADC recovery function boundaries missing")
    recovery = current[recovery_start:recovery_end]
    reset_pos = recovery.find("reset_line_toggle_dt(&adc_common_reset)")
    before = recovery.rfind("disable_and_clear_adc_irq();", 0, reset_pos)
    after = recovery.find("disable_and_clear_adc_irq();", reset_pos)
    program = recovery.find("program_adc_contract();", reset_pos)
    verify = recovery.find("adc_contract_readback_valid()", program)
    require(0 <= before < reset_pos < after < program < verify,
            "current ADC recovery ordering drift")

    current_node = block(board, "ams_current_sense: ams-current-sense")
    for token in (
        "high-controller = <&adc1>", "low-controller = <&adc2>",
        "&adc1_in3_pa3", "&adc2_in10_pc0",
        "resets = <&rctl STM32_RESET(APB2, 8)>",
        "adc-input-clock-hz = <108000000>", "adc-prescaler = <6>",
        "adc-clock-hz = <18000000>", "conversion-timeout-ms = <5>",
    ):
        require(token in current_node, f"current ADC board contract missing: {token}")
    for node in ("&adc1", "&adc2"):
        require('status = "disabled"' in block(board, node),
                f"generic current ADC device must remain disabled: {node}")
    # ADC3 is disabled by the SoC DTS and must not be overridden to okay in the
    # board file while the common ADCRST recovery architecture is in force.
    require(re.search(r"(?ms)^\s*&adc3\s*\{.*?status\s*=\s*\"okay\"", board) is None,
            "ADC3 cannot become live while current recovery owns common ADCRST")

    # CONFIG_PWM_CAPTURE globally causes pwm_stm32 to connect capture IRQs for
    # every enabled PWM timer. TIM3/4/5 are output-only fan timers, so the fan
    # adapter explicitly disables and pending-clears those unused NVIC lines.
    fan = (repo / "drivers/ams/fan_pwm_zephyr.c").read_text(encoding="utf-8")
    for token in (
        "disable_output_only_timer_irqs",
        "DT_IRQN(DT_NODELABEL(timers3)) == 29U",
        "DT_IRQN(DT_NODELABEL(timers4)) == 30U",
        "DT_IRQN(DT_NODELABEL(timers5)) == 50U",
        "irq_disable(irqs[i]);", "k_irq_clear_pending(irqs[i]);",
        "pwm_stm32_get_cycles_per_sec() ignores the",
    ):
        require(token in fan, f"fan timer IRQ/readiness hardening missing: {token}")

    # The lifecycle API is intentionally public only for startup/status. Raw
    # transfer calls remain private to drivers/ams until the single owner lands.
    public = (repo / "include/ams_platform/adbms_spi_lifecycle.h").read_text(encoding="utf-8")
    require("ams_adbms_spi_platform_init" in public and "ams_adbms_spi_platform_status" in public,
            "startup/status lifecycle API missing")
    require("ams_adbms_spi_write(" not in public and "ams_adbms_spi_write_read(" not in public,
            "raw ADBMS transport leaked into public platform surface")
    require("uint32_t integrity_violation_count;" in public,
            "ADBMS SPI lifecycle status lacks durable single-owner integrity evidence")

    prod = files_under(repo, ("app", "boards", "drivers", "include", "lib", "zephyr"))
    internal_include_hits = occurrences(repo, prod, re.compile(r"#\s*include\s*[<\"]adbms_spi_internal\.h"))
    require(all(hit.startswith("drivers/ams/adbms_spi_stm32.c:") for hit in internal_include_hits),
            "private ADBMS SPI header escaped its target adapter: " + str(internal_include_hits))

    for symbol in ("ams_adbms_spi_write", "ams_adbms_spi_write_read"):
        hits = occurrences(repo, prod, re.compile(rf"\b{symbol}\s*\("))
        bad = [h for h in hits if not h.startswith("drivers/ams/adbms_spi_stm32.c:")
               and not h.startswith("drivers/ams/adbms_spi_internal.h:")]
        require(not bad, f"Z-015 has a runtime/private-transfer caller for {symbol}: {bad}")

    generic_spi_hits = occurrences(repo, prod, re.compile(r"#\s*include\s*<zephyr/drivers/spi\.h>"))
    require(not generic_spi_hits,
            "generic Zephyr SPI API imported into current production tree: " + str(generic_spi_hits))

    main_c = (repo / "app/src/main.c").read_text(encoding="utf-8")
    require("ams_adbms_spi_platform_init();" in main_c,
            "startup does not prepare the Z-015 adapter")
    require(main_c.find("ams_adbms_spi_platform_init();") < main_c.find("ams_threads_start();"),
            "ADBMS SPI adapter must be prepared before runtime threads start")
    require("no runtime transfers" in main_c,
            "startup evidence no longer states zero-transfer boundary")

    runtime = (repo / "app/src/ams_threads.c").read_text(encoding="utf-8")
    require("watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_ADBMS" not in runtime,
            "placeholder ADBMS thread fabricated watchdog evidence")
    require("CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT" in runtime and
            "CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED" in runtime,
            "runtime compile-time capability truth missing")

    wake_hits = occurrences(
        repo, files_under(repo, ("drivers/ams", "include/ams_platform", "app")),
        re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*(?:wakeup|wake)[A-Za-z0-9_]*\s*\(", re.IGNORECASE),
    )
    require(not wake_hits, "Z-015 introduced a wake API/call: " + str(wake_hits))

    # No current-scope source may quietly enable vehicle authority or claim
    # physical ADBMS validation.
    for conf in sorted((repo / "app").glob("*.conf")):
        text = conf.read_text(encoding="utf-8")
        for forbidden in ("CONFIG_AMS_BMS_AUTHORITY=y", "CONFIG_AMS_BALANCE_AUTHORITY=y",
                          "CONFIG_AMS_ADBMS_SPI_PHYSICAL_VALIDATED=y"):
            require(forbidden not in text, f"unsafe Z-015 config claim in {conf.name}: {forbidden}")

    print("PASS: Z-015 private SPI6 + HAL-driver source/architecture safety contract")
    print("  stock spi_stm32 ownership: structurally absent")
    print("  SPI6 IRQ/DMA/async path: absent; NVIC line disabled + pending-cleared")
    print("  transfer engine: bounded synchronous owner; illegal/reentrant attempts durably counted")
    print("  current ADC: private bounded polling; transient timeout recovery; generic adc_stm32 absent")
    print("  fan: output-only TIM3/4/5 capture IRQs disabled + pending-cleared")
    print("  runtime: adapter init/status only; zero transfers/wake/ADBMS evidence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
