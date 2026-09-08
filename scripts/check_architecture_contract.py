#!/usr/bin/env python3

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


def source_files(base: Path):
    if not base.is_dir():
        return
    for path in base.rglob("*"):
        if path.is_file() and path.suffix in (".c", ".h"):
            yield path




def strip_c_comments(text: str) -> str:
    """Remove C/C++ comments while preserving strings, chars and newlines.

    Contract scans below are intended to inspect executable source, not oracle
    provenance prose. Preserving newlines keeps diagnostics and preprocessor
    structure readable while preventing comment-only HAL/RTOS names from being
    mistaken for dependencies.
    """
    out: list[str] = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if state == "code":
            if ch == '/' and nxt == '*':
                out.extend((' ', ' '))
                i += 2
                state = "block"
                continue
            if ch == '/' and nxt == '/':
                out.extend((' ', ' '))
                i += 2
                state = "line"
                continue
            if ch == '"':
                out.append(ch)
                i += 1
                state = "string"
                continue
            if ch == "'":
                out.append(ch)
                i += 1
                state = "char"
                continue
            out.append(ch)
            i += 1
            continue

        if state == "block":
            if ch == '*' and nxt == '/':
                out.extend((' ', ' '))
                i += 2
                state = "code"
            else:
                out.append('\n' if ch == '\n' else ' ')
                i += 1
            continue

        if state == "line":
            if ch == '\n':
                out.append('\n')
                i += 1
                state = "code"
            else:
                out.append(' ')
                i += 1
            continue

        # Preserve literals exactly; escaped quotes do not terminate them.
        out.append(ch)
        if ch == '\\' and i + 1 < len(text):
            out.append(text[i + 1])
            i += 2
            continue
        if state == "string" and ch == '"':
            state = "code"
        elif state == "char" and ch == "'":
            state = "code"
        i += 1

    return ''.join(out)

def production_sources(root: Path):
    for base in (
        root / "app",
        root / "drivers",
        root / "boards",
        root / "lib" / "ams_core",
    ):
        yield from source_files(base)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    core = repo / "lib" / "ams_core"
    app = repo / "app"
    platform_include = repo / "include" / "ams_platform"
    drivers = repo / "drivers" / "ams"
    board = repo / "boards" / "drexel" / "der26_ams"
    dts = board / "der26_ams.dts"
    app_cmake = app / "CMakeLists.txt"
    driver_cmake = drivers / "CMakeLists.txt"
    board_cmake = board / "CMakeLists.txt"
    module_yml = repo / "zephyr" / "module.yml"
    module_cmake = repo / "zephyr" / "CMakeLists.txt"
    fail_low = board / "ams_fail_low_stm32.c"
    bms_adapter = drivers / "bms_ok_zephyr.c"
    bindings_used = build / "zephyr" / "dts_bindings_used.txt"
    generated_dts = build / "zephyr" / "zephyr.dts"
    all_contracts = repo / "scripts" / "check_all_contracts.py"
    null_checker = repo / "scripts" / "check_null_platform_core.py"
    null_cmake = repo / "tests" / "null_platform" / "CMakeLists.txt"
    null_smoke = repo / "tests" / "null_platform" / "null_platform_smoke.c"
    null_ci = repo / ".github" / "workflows" / "portable-core-null-platform.yml"

    binding_files = (
        repo / "dts/bindings/ams/drexel,ams-safety-io.yaml",
        repo / "dts/bindings/ams/drexel,ams-adbms-interface.yaml",
        repo / "dts/bindings/ams/drexel,ams-current-sense.yaml",
        repo / "dts/bindings/ams/drexel,ams-fan-bank.yaml",
        repo / "dts/bindings/ams/drexel,ams-imd.yaml",
    )

    required = (
        dts,
        app_cmake,
        driver_cmake,
        board_cmake,
        module_yml,
        module_cmake,
        fail_low,
        bms_adapter,
        generated_dts,
        all_contracts,
        null_checker,
        null_cmake,
        null_smoke,
        null_ci,
        repo / "dts/bindings/vendor-prefixes.txt",
        *binding_files,
    )
    for path in required:
        require(path.is_file(), f"missing architecture artifact: {path}")

    # ------------------------------------------------------------------
    # Portable core: source-level defense plus a separate *real build* gate.
    # ------------------------------------------------------------------
    core_forbidden = re.compile(
        r'#include\s*[<"](?:zephyr/|FreeRTOS|task\.h|cmsis|stm32|soc\.h|ams_platform/)|'
        r'\bHAL_[A-Za-z0-9_]+\s*\(|'
        r'\b(?:ADC|CAN|GPIO|IWDG|SPI|TIM)_HandleTypeDef\b|'
        r'\b(?:k_thread|k_mutex|k_sem|k_work|k_sleep|k_poll|k_timer|k_msgq|k_fifo|k_queue)\b|'
        r'\b(?:K_MSEC|K_USEC|K_FOREVER|K_NO_WAIT)\b|'
        r'\bDT_[A-Z0-9_]+\b|'
        r'\bDEVICE_DT_[A-Z0-9_]+\b|'
        r'\bLL_[A-Za-z0-9_]+\b'
    )
    for path in source_files(core):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        match = core_forbidden.search(code)
        require(
            match is None,
            f"portable ams_core platform dependency in {path.relative_to(repo)}: "
            f"{match.group(0) if match else ''}",
        )

    # Public app-facing platform interfaces are themselves platform-neutral.
    # Zephyr structs/macros stay private to drivers/ams implementations.
    platform_header_forbidden = re.compile(
        r'#include\s*[<"](?:zephyr/|FreeRTOS|task\.h|cmsis|stm32|soc\.h)|'
        r'\bstruct\s+(?:device|gpio_dt_spec|adc_dt_spec|pwm_dt_spec|spi_dt_spec|can_frame|wdt_timeout_cfg)\b|'
        r'\bDT_[A-Z0-9_]+\b|\bDEVICE_DT_[A-Z0-9_]+\b|'
        r'\b(?:k_timeout_t|k_tid_t|atomic_t)\b'
    )
    for path in source_files(platform_include):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        match = platform_header_forbidden.search(code)
        require(
            match is None,
            f"public ams_platform interface leaks Zephyr/MCU types in {path.relative_to(repo)}: "
            f"{match.group(0) if match else ''}",
        )

    # Legacy FreeRTOS/CMSIS-RTOS/HAL dependencies must not enter new production
    # layers. HAL names in comments documenting oracle provenance are allowed;
    # actual includes/calls/types are not.
    legacy_include = re.compile(
        r'#include\s*[<"](?:FreeRTOS|task\.h|cmsis_os|stm32f7xx_hal)'
    )
    legacy_call_or_type = re.compile(
        r'\bHAL_[A-Za-z0-9_]+\s*\(|'
        r'\b(?:ADC|CAN|GPIO|IWDG|SPI|TIM)_HandleTypeDef\b|'
        r'\b(?:TaskHandle_t|SemaphoreHandle_t|EventGroupHandle_t|QueueHandle_t)\b'
    )
    for path in production_sources(repo):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        match = legacy_include.search(code) or legacy_call_or_type.search(code)
        require(
            match is None,
            f"legacy HAL/RTOS dependency in {path.relative_to(repo)}: "
            f"{match.group(0) if match else ''}",
        )

    # App owns orchestration/safety policy, not hardware mappings or Zephyr
    # driver APIs. Kernel primitives are allowed here; hardware-driver/DT APIs
    # are not.
    app_hw_forbidden = re.compile(
        r'#include\s*[<"]zephyr/(?:drivers/|devicetree\.h)|'
        r'\b(?:gpio|adc|pwm|spi|can|wdt)_(?:dt_spec|pin|port|read|write|set|get|configure|transceive|send|recover|feed)\b|'
        r'\b(?:GPIO|ADC|PWM|SPI|CAN|WDT)_DT_SPEC\b|'
        r'\bDT_(?:NODELABEL|PATH|ALIAS|GPIO_|PWMS_|IO_CHANNELS_|PROP|REG|IRQ|SAME_NODE|NODE_HAS_PROP)'
    )
    for path in source_files(app / "src"):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        match = app_hw_forbidden.search(code)
        require(
            match is None,
            f"app layer owns hardware/DT detail in {path.relative_to(repo)}: "
            f"{match.group(0) if match else ''}",
        )

    # Direct STM32/CMSIS ownership is intentionally narrow and file-scoped.
    # The board fail-low primitive owns pre-kernel PE0 safety access. Z-015 also
    # has three audited platform seams: private SPI6 LL, private ADC1/2 LL, and
    # fan-timer NVIC pending-clear hardening. No application/policy/core file may
    # acquire direct MCU ownership merely because these exceptions exist.
    direct_mcu = re.compile(
        r'#include\s*[<"]soc\.h[>"]|'
        r'\b(?:RCC|GPIO[A-K]|ADC[0-9]*|TIM[0-9]+|CAN[0-9]*|SPI[0-9]+|IWDG|SCB|EXTI|DMA[0-9]*)->|'
        r'\bLL_[A-Za-z0-9_]+\s*\(|'
        r'\bNVIC_[A-Za-z0-9_]+\s*\('
    )
    expected_direct_owners = {
        fail_low.resolve(),
        (drivers / "adbms_spi_stm32.c").resolve(),
        (drivers / "current_adc_stm32.c").resolve(),
        (drivers / "fan_pwm_zephyr.c").resolve(),
    }
    direct_owners = set()
    for path in production_sources(repo):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        if direct_mcu.search(code):
            direct_owners.add(path.resolve())
    require(
        direct_owners == expected_direct_owners,
        "direct STM32 ownership set drifted; expected "
        + ", ".join(sorted(str(p.relative_to(repo)) for p in expected_direct_owners))
        + "; got "
        + ", ".join(sorted(str(p.relative_to(repo)) for p in direct_owners)),
    )
    fail_low_text = fail_low.read_text(encoding="utf-8")
    for token in (
        "RCC->AHB1ENR",
        "GPIOE->BSRR",
        "GPIOE->MODER",
        "__DSB();",
        "__ISB();",
        "SYS_INIT(ams_bms_ok_early_init, PRE_KERNEL_1, 0);",
    ):
        require(token in fail_low_text, f"board emergency fail-low primitive lost: {token}")

    # Normal BMS_OK ownership is a typed Zephyr GPIO adapter with no API that
    # can assert the line during the no-authority stage.
    bms = bms_adapter.read_text(encoding="utf-8")
    bms_public = (platform_include / "bms_ok.h").read_text(encoding="utf-8")
    require(
        "GPIO_DT_SPEC_GET(AMS_SAFETY_NODE, bms_ok_gpios)" in bms,
        "normal BMS_OK path must use typed gpio_dt_spec",
    )
    require(
        "ams_bms_ok_force_low_direct();" in bms,
        "normal BMS_OK adapter must fall back to emergency fail-low",
    )
    require(
        "ams_bms_ok_platform_init_low" in bms_public,
        "public BMS_OK interface missing low-only initialization",
    )
    for forbidden in ("set_high", "assert_high", "enable_authority", "set_state"):
        require(
            forbidden not in bms_public.lower(),
            f"no-authority BMS_OK public interface exposes suspicious authority token: {forbidden}",
        )

    # Production runtime stays static/heap-free and avoids generic system
    # workqueues for safety-critical periodic actors.
    alloc_pattern = re.compile(
        r'\b(?:malloc|calloc|realloc|free|k_malloc|k_calloc|k_realloc|k_free)\s*\('
    )
    for path in production_sources(repo):
        text = path.read_text(encoding="utf-8", errors="replace")
        code = strip_c_comments(text)
        require(
            alloc_pattern.search(code) is None,
            f"dynamic allocation introduced in {path.relative_to(repo)}",
        )
    threads = (app / "src/ams_threads.c").read_text(encoding="utf-8")
    for token in (
        "k_work_submit",
        "k_work_schedule",
        "k_work_reschedule",
        "k_sys_work_q",
    ):
        require(
            token not in threads,
            f"critical runtime must not use generic system workqueue: {token}",
        )

    # Hardware contracts are typed and consumed through Zephyr dt_spec helpers
    # instead of /zephyr,user.
    board_text = dts.read_text(encoding="utf-8")
    require(re.search(r"\bzephyr,user\s*\{", board_text) is None, "board hardware contracts regressed to /zephyr,user")
    compatibles = (
        "drexel,ams-safety-io",
        "drexel,ams-adbms-interface",
        "drexel,ams-current-sense",
        "drexel,ams-fan-bank",
        "drexel,ams-imd",
    )
    for compatible in compatibles:
        require(
            f'compatible = "{compatible}"' in board_text,
            f"typed board interface missing: {compatible}",
        )

    # Each custom binding inherits base.yaml. v4.4 supports const for exact
    # string arrays; use that to freeze semantic channel/name order. Exact
    # phandle-array cardinality is additionally BUILD_ASSERTed in C because
    # v4.4 does not have the later min-len/max-len schema keywords.
    for binding in binding_files:
        text = binding.read_text(encoding="utf-8")
        require("base.yaml" in text, f"binding lacks base.yaml: {binding.name}")
    current_binding = binding_files[2].read_text(encoding="utf-8")
    fan_binding = binding_files[3].read_text(encoding="utf-8")
    require(
        "high-controller:" in current_binding and "low-controller:" in current_binding
        and "pinctrl-device.yaml" in current_binding and "reset-device.yaml" in current_binding,
        "current binding must freeze private ADC1/ADC2 ownership plus pinctrl/reset metadata",
    )
    require(
        'const: ["fan1", "fan2", "fan3", "fan4", "fan5", "fan6"]' in fan_binding,
        "fan binding must freeze six-zone semantic order",
    )

    current = (drivers / "current_adc_stm32.c").read_text(encoding="utf-8")
    fan = (drivers / "fan_pwm_zephyr.c").read_text(encoding="utf-8")
    imd = (drivers / "imd_capture_zephyr.c").read_text(encoding="utf-8")
    require(
        "DT_PHANDLE(CURRENT_ADC_NODE, high_controller)" in current
        and "DT_PHANDLE(CURRENT_ADC_NODE, low_controller)" in current
        and "adc_read_async" not in current and "k_poll(" not in current,
        "current adapter must privately own typed ADC1/ADC2 metadata without Zephyr async ADC context",
    )
    require(
        "PWM_DT_SPEC_GET_BY_NAME" in fan and "DT_PROP_LEN(AMS_FAN_NODE, pwms)" in fan,
        "fan adapter must use named pwm_dt_spec consumers and freeze six entries",
    )
    require(
        "PWM_DT_SPEC_GET(IMD_NODE)" in imd
        and "GPIO_DT_SPEC_GET(IMD_NODE, status_gpios)" in imd,
        "IMD adapter must use typed PWM/GPIO dt_spec consumers",
    )

    # Build layering: ordinary platform hardware is created by the repository's
    # Zephyr module while CMake is still in kernel mode.  Board safety is
    # created exactly once by Zephyr's board integration.  The application
    # must not manually add either hardware directory after find_package().
    cmake = app_cmake.read_text(encoding="utf-8")
    module_cmake_text = module_cmake.read_text(encoding="utf-8")
    driver_cmake_text = driver_cmake.read_text(encoding="utf-8")
    board_cmake_text = board_cmake.read_text(encoding="utf-8")
    require(
        'list(APPEND DTS_ROOT "${DRG27_AMS_ROOT}")' in cmake,
        "repository Devicetree bindings are not registered as DTS_ROOT",
    )
    require(
        '"${DRG27_AMS_ROOT}/drivers/ams"' not in cmake
        and '"${DRG27_AMS_ROOT}/boards/drexel/der26_ams"' not in cmake,
        "app CMake must not manually add platform or board directories",
    )
    require(
        "TARGET ams_platform" in cmake and "TARGET ams_board_safety" in cmake,
        "app CMake must verify module/board integration targets exist",
    )
    require(
        "target_link_libraries(ams_platform PRIVATE ams_core)" in cmake
        and "target_link_libraries(app PRIVATE ams_core)" in cmake,
        "CMake dependency direction to ams_core is not explicit",
    )
    require(
        '"${DRG27_AMS_MODULE_ROOT}/drivers/ams"' in module_cmake_text,
        "Zephyr module integration must own drivers/ams composition",
    )
    require(
        "boards/drexel/der26_ams" not in module_cmake_text,
        "Zephyr module must not manually compose the board CMake directory",
    )
    require(
        "zephyr_library_named(ams_platform)" in driver_cmake_text,
        "drivers/ams must own the ams_platform Zephyr library",
    )
    require(
        "zephyr_library_named(ams_board_safety)" in board_cmake_text,
        "board must own separate emergency safety library",
    )
    require(
        "CMAKE_CURRENT_LIST_DIR}/../.." in driver_cmake_text,
        "platform CMake must derive its repository root without app-scope variables",
    )
    require(
        "CMAKE_CURRENT_LIST_DIR}/../../.." in board_cmake_text,
        "board CMake must derive its repository root without app-scope variables",
    )
    core_cmake = (core / "CMakeLists.txt").read_text(encoding="utf-8")
    for forbidden in ("zephyr_library", "find_package(Zephyr", "ams_platform", "drivers/ams"):
        require(forbidden not in core_cmake, f"ams_core CMake leaked platform dependency: {forbidden}")

    module = module_yml.read_text(encoding="utf-8")
    require(
        "cmake: zephyr" in module,
        "module metadata must load zephyr/CMakeLists.txt in kernel CMake mode",
    )
    require("board_root: ." in module and "dts_root: ." in module,
            "module metadata must export board and custom Devicetree roots")

    generated = generated_dts.read_text(encoding="utf-8", errors="replace")
    for compatible in compatibles:
        require(
            f'compatible = "{compatible}"' in generated,
            f"generated DTS missing typed interface: {compatible}",
        )
    require(re.search(r"\bzephyr,user\s*\{", generated) is None, "generated DTS still contains /zephyr,user AMS hardware")

    # If Zephyr emitted its binding-use report, prove the custom schemas were
    # actually discovered rather than merely present in the repository.
    if bindings_used.is_file():
        used = bindings_used.read_text(encoding="utf-8", errors="replace")
        for binding in binding_files:
            require(binding.name in used, f"Zephyr did not report custom binding as used: {binding.name}")

    # Mechanical portability gate: the standalone project must build the real
    # core CMake with no Zephyr package, and CI/unified gating must invoke it.
    null_cmake_text = null_cmake.read_text(encoding="utf-8")
    null_checker_text = null_checker.read_text(encoding="utf-8")
    null_ci_text = null_ci.read_text(encoding="utf-8")
    all_contracts_text = all_contracts.read_text(encoding="utf-8")
    require(
        'add_subdirectory("${AMS_CORE_DIR}"' in null_cmake_text
        and re.search(r"^\s*find_package\(Zephyr", null_cmake_text, re.MULTILINE) is None,
        "null-platform project must compile the real ams_core without Zephyr CMake",
    )
    for token in (
        "CMAKE_DISABLE_FIND_PACKAGE_Zephyr=TRUE",
        "compile_commands.json",
        "ctest",
        "FORBIDDEN_COMPILE_TOKENS",
    ):
        require(token in null_checker_text, f"null-platform checker missing isolation proof: {token}")
    require(
        "scripts/check_null_platform_core.py ." in null_ci_text,
        "native CI must execute the real null-platform core gate",
    )
    require(
        "check_null_platform_core.py" in all_contracts_text,
        "unified release gate must include null-platform core validation",
    )

    print("PASS: Z-015 application/hardware/Zephyr architecture contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
