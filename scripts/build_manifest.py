#!/usr/bin/env python3

import argparse
import json
import re
import subprocess
from pathlib import Path
import sys


THREADS = [
    # name, priority, period, stale timeout, startup grace, stack,
    # enabled, safety-heartbeat-required, safety-evidence-ready, v2.6.27 stack lower bound
    ("ams_safety", 0, 50, 0, 0, 2048, True, False, False, 1024),
    ("ams_current", 2, 20, 200, 3000, 2048, True, True, False, 1024),
    ("ams_adbms", 3, 100, 3000, 3000, 8192, True, True, False, 6144),
    ("ams_can", 4, 100, 2000, 3000, 8192, True, True, False, 6144),
    ("ams_estimator", 6, 100, 500, 3000, 8192, True, False, False, 6144),
    ("ams_fan", 8, 200, 1000, 3000, 1536, True, True, True, 768),
    ("ams_air", 8, 500, 0, 0, 1536, False, False, False, 768),
    ("ams_imd", 9, 100, 500, 3000, 1536, True, True, True, 768),
    ("ams_diag", 12, 0, 0, 0, 4096, True, False, False, 2048),
]



def read_config_string(config: str, symbol: str):
    match = re.search(
        rf'^{re.escape(symbol)}="([^"]+)"$',
        config,
        flags=re.MULTILINE,
    )

    return match.group(1) if match else None


def dts_block(text: str, label: str) -> str:
    start = text.find(label)
    if start < 0:
        return ""
    brace = text.find("{", start)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return ""


def dts_enabled(text: str, label: str) -> bool:
    return 'status = "okay"' in dts_block(text, label)


def git_output(repo: Path, *args: str) -> str | None:
    """Return git output when the package is inside a worktree.

    Release ZIPs intentionally do not contain `.git`, so target contract
    generation must remain usable on an independently extracted package.
    """
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=repo,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    build = args.build_dir.resolve()

    config_path = build / "zephyr" / ".config"
    dts_path = build / "zephyr" / "zephyr.dts"

    if not config_path.is_file():
        raise SystemExit(f"missing {config_path}")

    if not dts_path.is_file():
        raise SystemExit(f"missing {dts_path}")

    config = config_path.read_text(encoding="utf-8")
    dts = dts_path.read_text(encoding="utf-8")

    revision_match = re.search(
        r"^\s*revision:\s*(\S+)\s*$",
        (repo / "west.yml").read_text(encoding="utf-8"),
        flags=re.MULTILINE,
    )

    zephyr_revision = (
        revision_match.group(1)
        if revision_match
        else "unknown"
    )

    git_commit = git_output(repo, "rev-parse", "HEAD")
    git_status = git_output(repo, "status", "--porcelain")
    git_dirty = None if git_status is None else bool(git_status)

    artifact_sizes = {}

    for name in ("zephyr.elf", "zephyr.bin", "zephyr.hex"):
        artifact = build / "zephyr" / name

        if artifact.is_file():
            artifact_sizes[name] = artifact.stat().st_size

    manifest = {
        "schema_version": 1,
        "migration_stage": "Z-015",
        "oracle": {
            "package": "v2.6.27",
            "firmware": "0.5.30",
        },
        "source": {
            "git_commit": git_commit,
            "git_dirty": git_dirty,
        },
        "zephyr": {
            "revision": zephyr_revision,
            "board": read_config_string(config, "CONFIG_BOARD"),
            "soc": read_config_string(config, "CONFIG_SOC"),
        },
        "authority": {
            "bms_ok": (
                "CONFIG_AMS_BMS_AUTHORITY=y"
                in config
            ),
            "balancing": (
                "CONFIG_AMS_BALANCE_AUTHORITY=y"
                in config
            ),
        },
        "migration_capabilities": {
            "bms_ok_platform_adapter_present": "CONFIG_AMS_CAP_BMS_OK_PLATFORM_ADAPTER_PRESENT=y" in config,
            "current_adc_adapter_present": "CONFIG_AMS_CAP_CURRENT_ADC_ADAPTER_PRESENT=y" in config,
            "current_actor_live": "CONFIG_AMS_CAP_CURRENT_ACTOR_LIVE=y" in config,
            "current_safety_evidence": "CONFIG_AMS_CAP_CURRENT_SAFETY_EVIDENCE=y" in config,
            "adbms_spi_adapter_present": "CONFIG_AMS_CAP_ADBMS_SPI_ADAPTER_PRESENT=y" in config,
            "adbms_spi_physical_validated": "CONFIG_AMS_CAP_ADBMS_SPI_PHYSICAL_VALIDATED=y" in config,
            "adbms_actor_live": "CONFIG_AMS_CAP_ADBMS_ACTOR_LIVE=y" in config,
            "adbms_safety_evidence": "CONFIG_AMS_CAP_ADBMS_SAFETY_EVIDENCE=y" in config,
            "temperature_safety_evidence": "CONFIG_AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE=y" in config,
            "can_adapter_present": "CONFIG_AMS_CAP_CAN_ADAPTER_PRESENT=y" in config,
            "can_actor_live": "CONFIG_AMS_CAP_CAN_ACTOR_LIVE=y" in config,
            "can_safety_evidence": "CONFIG_AMS_CAP_CAN_SAFETY_EVIDENCE=y" in config,
            "fan_pwm_adapter_present": "CONFIG_AMS_CAP_FAN_PWM_ADAPTER_PRESENT=y" in config,
            "fan_actor_live": "CONFIG_AMS_CAP_FAN_ACTOR_LIVE=y" in config,
            "fan_safety_evidence": "CONFIG_AMS_CAP_FAN_SAFETY_EVIDENCE=y" in config,
            "fan_physical_validated": "CONFIG_AMS_CAP_FAN_PHYSICAL_VALIDATED=y" in config,
            "fan_target_validation_claim": "CONFIG_AMS_FAN_TARGET_VALIDATED=y" in config,
            "imd_capture_adapter_present": "CONFIG_AMS_CAP_IMD_CAPTURE_ADAPTER_PRESENT=y" in config,
            "imd_actor_live": "CONFIG_AMS_CAP_IMD_ACTOR_LIVE=y" in config,
            "imd_safety_evidence": "CONFIG_AMS_CAP_IMD_SAFETY_EVIDENCE=y" in config,
            "imd_physical_validated": "CONFIG_AMS_CAP_IMD_PHYSICAL_VALIDATED=y" in config,
            "imd_target_validation_claim": "CONFIG_AMS_IMD_TARGET_VALIDATED=y" in config,
            "watchdog_adapter_present": "CONFIG_AMS_CAP_WATCHDOG_ADAPTER_PRESENT=y" in config,
            "watchdog_active": "CONFIG_AMS_CAP_WATCHDOG_ACTIVE=y" in config,
            "watchdog_full_oracle_coverage": "CONFIG_AMS_CAP_WATCHDOG_FULL_ORACLE_COVERAGE=y" in config,
            "watchdog_physical_validated": "CONFIG_AMS_CAP_WATCHDOG_PHYSICAL_VALIDATED=y" in config,
            "watchdog_target_validation_claim": "CONFIG_AMS_WATCHDOG_TARGET_VALIDATED=y" in config,
        },
        "architecture": {
            "portable_core_platform_independent": True,
            "portable_core_null_platform_gate_required": True,
            "portable_core_null_platform_gate": "scripts/check_null_platform_core.py",
            "portable_core_native_ci": ".github/workflows/portable-core-null-platform.yml",
            "application_owns_direct_mcu_registers": False,
            "application_owns_gpio_devicetree_mapping": False,
            "normal_bms_ok_gpio_owner": "drivers/ams/bms_ok_zephyr.c",
            "approved_direct_register_owner": "boards/drexel/der26_ams/ams_fail_low_stm32.c",
            "pre_kernel_fail_low_registration_owned_by_board_layer": True,
            "typed_devicetree_consumers": True,
            "zephyr_user_ams_hardware_contracts": False,
            "custom_bindings": [
                "drexel,ams-safety-io",
                "drexel,ams-adbms-interface",
                "drexel,ams-current-sense",
                "drexel,ams-fan-bank",
                "drexel,ams-imd",
            ],
            "current_adc_uses_named_adc_dt_spec": False,
            "current_adc_uses_typed_private_controller_phandles": True,
            "current_adc_private_backend": "drivers/ams/current_adc_stm32.c",
            "current_adc_generic_zephyr_driver_owned": False,
            "current_adc_irq_transport": False,
            "current_adc_dma_transport": False,
            "current_adc_async_transport": False,
            "fan_uses_named_pwm_dt_spec": True,
            "fan_output_timer_capture_irqs_disabled": True,
            "imd_uses_pwm_and_gpio_dt_spec": True,
            "platform_adapter_build_layer_separate": True,
            "board_emergency_primitive_build_layer_separate": True,
            "unified_contract_gate": "scripts/check_all_contracts.py",
            "adbms_spi_private_backend": "drivers/ams/adbms_spi_stm32.c",
            "adbms_spi_generic_zephyr_driver_owned": False,
            "adbms_spi_stock_dt_device_enabled": False,
            "adbms_spi_irq_transport": False,
            "adbms_spi_dma_transport": False,
            "adbms_spi_async_transport": False,
            "adbms_spi_owner_rpc_required_before_diagnostics": True,
        },
        "board_contract": {
            "mcu": "STM32F767ZIT6",
            "hse_hz": 8_000_000,
            "sysclk_hz": 216_000_000,
            "sram0_bytes": 384 * 1024,
            "dtcm_bytes": 128 * 1024,
            "bms_ok": "PE0",
            "adbms_cs_a": "PE2",
            "adbms_cs_b": "PE4",
            "can1_rx": "PD0",
            "can1_tx": "PD1",
            "spi6_miso": "PG12",
            "spi6_sck": "PG13",
            "spi6_mosi": "PG14",
            "spi6_input_clock_hz": 108_000_000,
            "spi6_prescaler": 256,
            "spi6_achieved_hz": 421_875,
            "spi6_mode": 3,
            "spi6_timeout_ms": 500,
            "spi6_max_transfer_bytes": 512,
            "spi6_read_dummy_byte": 255,
            "current_high": "PA3/ADC1_IN3",
            "current_low": "PC0/ADC2_IN10",
            "current_adc_input_clock_hz": 108_000_000,
            "current_adc_prescaler": 6,
            "current_adc_clock_hz": 18_000_000,
            "current_adc_common_reset": "APB2_ADCRST_bit8_ADC1_ADC2_ADC3",
            "current_adc_shared_irq": 18,
            "imd_pwm": "PA5/TIM2_CH1",
            "imd_ok_hs": "PC5",
            "uart3_tx": "PD8",
            "uart3_rx": "PD9",
        },
        "runtime_threads": [
            {
                "name": name,
                "priority": priority,
                "period_ms": period,
                "stale_deadline_ms": stale,
                "startup_grace_ms": grace,
                "nominal_stack_bytes": stack,
                "enabled": enabled,
                "safety_heartbeat_required": safety_required,
                "safety_evidence_ready": safety_evidence_ready,
                "v2627_stack_lower_bound_bytes": oracle_stack,
            }
            for (
                name,
                priority,
                period,
                stale,
                grace,
                stack,
                enabled,
                safety_required,
                safety_evidence_ready,
                oracle_stack,
            ) in THREADS
        ],
        "freertos_runtime_parity": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "normal_periods_matched": True,
            "relative_priority_order_matched": True,
            "heartbeat_startup_grace_ms": 3000,
            "heartbeat_timeouts_ms": {
                "adbms": 3000,
                "current": 200,
                "temperature": 3000,
                "can": 2000,
                "logger": 2000,
                "imd": 500,
                "fan": 1000,
                "estimator": 500,
            },
            "current_window_mutex_timeout_ms": 20,
            "adbms_mutex_timeout_ms": 500,
            "air_placeholder_started": False,
            "imd_placeholder_started": False,
            "temperature_heartbeat_integrated": False,
            "watchdog_policy_integrated": True,
            "bms_authority_enabled": False,
            "balance_authority_enabled": False,
            "placeholder_runtime_cycles_are_safety_evidence": False,
            "fan_real_workload_is_safety_evidence": True,
            "imd_real_workload_is_safety_evidence": True,
            "imd_runtime_enabled_for_no_authority_validation": True,
            "imd_target_physically_validated": False,
            "heartbeat_count_saturates": True,
            "unseen_heartbeat_stale_at_grace_boundary": True,
            "startup_epoch_precedes_thread_construction": True,
            "safety_supervisor_overrun_reanchors_like_freertos": True,
            "fan_overrun_reanchors_like_freertos": True,
            "placeholder_absolute_release_scheduling_is_stage_divergence": True,
        },
        "safety_integrity": {
            "kernel_assertions": "CONFIG_ASSERT=y" in config,
            "arm_mpu": "CONFIG_ARM_MPU=y" in config,
            "hardware_stack_protection": "CONFIG_HW_STACK_PROTECTION=y" in config,
            "application_heap_bytes": 0,
            "bms_ok_fail_low_barriers": "DSB+ISB",
            "fatal_handler_forces_bms_low_before_halt": True,
            "retained_panic_record_ported": False,
            "fault_log_ported": False,
            "reset_cause_log_ported": False,
            "watchdog_reset_cause_capture_ported": True,
            "proactive_stack_integrity_ported": True,
            "watchdog_sole_feeder": "ams_safety",
            "hardware_watchdog_ported": True,
            "normal_bms_supervisor_authority_ported": False,
            "vehicle_authority_eligible": False,
        },
        "measurement_store": {
            "buffer_count": 2,
            "snapshot_max_bytes": 2048,
            "store_max_bytes": 4096,
            "reader_pinning": True,
            "copy_outside_lock": True,
            "injected_metadata_lock": True,
            "heap_required": False,
        },
        "current_window": {
            "max_sample_age_ms": 100,
            "max_integration_gap_ms": 100,
            "out_of_order_boundary_rejected": True,
            "carried_sensor_metadata": True,
            "sticky_mixed_range": True,
            "calibration_provenance_carried": True,
            "heap_required": False,
        },
        "power_core": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "sop_exact_oracle_copy": True,
            "soh_exact_oracle_copy": True,
            "fuse_observer_exact_oracle_copy": True,
            "power_strategy_ported": False,
            "power_state_integration_ported": False,
            "power_can_ported": False,
            "heap_required": False,
        },
        "adbms_spi": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "stage": "Z-015 transport substrate only",
            "implementation": "private STM32F767 bounded synchronous polling backend",
            "generic_zephyr_spi_enabled": "CONFIG_SPI=y" in config,
            "stock_spi6_dt_enabled": dts_enabled(dts, "spi6:"),
            "input_clock_hz": 108_000_000,
            "prescaler": 256,
            "achieved_frequency_hz": 421_875,
            "mode": 3,
            "data_bits": 8,
            "bit_order": "MSB_first",
            "software_nss": True,
            "cs_a": "PE2_active_low",
            "cs_b": "PE4_active_low",
            "timeout_ms": 500,
            "timeout_policy": "one_wrap_safe_absolute_transaction_deadline",
            "max_transfer_bytes": 512,
            "read_dummy_byte": 255,
            "write_drains_rx": True,
            "irq_enabled": False,
            "dma_enabled": False,
            "async_enabled": False,
            "recovery": "CS_high_then_SPI6_RCC_reset_reconfigure_readback",
            "timeout_is_terminal_by_default": False,
            "recovery_failure_latches_faulted": True,
            "runtime_transfer_callers": 0,
            "linked_runtime_transfer_entrypoints_required_absent": True,
            "integrity_violation_counter": "saturating",
            "startup_initializes_without_transfer": True,
            "wake_api_present": False,
            "adbms_actor_live": False,
            "adbms_safety_evidence": False,
            "physical_validation_complete": False,
            "future_cli_requires_owner_rpc": True,
            "freertos_recursive_mutex_ported": False,
        },
        "current_adc": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "high_range": "PA3/ADC1_IN3/+/-800A",
            "low_range": "PC0/ADC2_IN10/+/-50A",
            "acquisition_order": "high_then_low",
            "resolution_bits": 12,
            "adc_clock_source": "SYNC",
            "adc_prescaler": 6,
            "adc_clock_hz": 18_000_000,
            "acquisition_ticks": 480,
            "completion_timeout_ms": 5,
            "completion_timeout_semantics": "HAL_F7_elapsed_gt_5ms_with_EOC_recheck",
            "success_flag_clear": "STRT_plus_EOC_before_DR_read",
            "completion_mechanism": "private_STM32_LL_bounded_poll",
            "generic_zephyr_adc_enabled": "CONFIG_ADC=y" in config,
            "async_enabled": "CONFIG_ADC_ASYNC=y" in config,
            "dma_enabled": "CONFIG_ADC_STM32_DMA=y" in config,
            "irq_enabled": False,
            "shared_irq": 18,
            "common_reset_owns_adc1_adc2_adc3": True,
            "adc3_must_remain_disabled": True,
            "timeout_recovery": "ADC_common_RCC_reset_reconfigure_readback_then_ready",
            "timeout_is_terminal_by_default": False,
            "recovery_failure_latches_faulted": True,
            "integrity_violation_counter": "saturating",
            "oversampling": 0,
            "current_thread_integrated": False,
            "current_window_integrated": False,
            "safety_publication_integrated": False,
            "filtered_current_is_safety_authority": False,
        },
        "fan_pwm": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "zones": 6,
            "timer_clock_hz": 108_000_000,
            "timer_prescaler": 0,
            "legacy_arr": 3360,
            "zephyr_period_cycles": 3361,
            "frequency_hz": 108_000_000 / 3361,
            "polarity": "active_high_pwm1",
            "mapping": [
                "PA7/TIM3_CH2",
                "PB1/TIM3_CH4",
                "PD14/TIM4_CH3",
                "PD15/TIM4_CH4",
                "PA0/TIM5_CH1",
                "PA1/TIM5_CH2",
            ],
            "ramp_start_c": 35.0,
            "max_c": 50.0,
            "minimum_running_percent": 25.0,
            "charge_warm_percent": 35.0,
            "off_hysteresis_c": 3.0,
            "missing_temperature_behavior": "100_percent_fail_max",
            "startup_channel_failure": "soft_process_fault_retry",
            "timer_platform_failure": "fatal_fail_low",
            "output_only_timer_irqs_disabled": [29, 30, 50],
            "imd_timer_irq_preserved": 28,
            "physical_feedback": False,
            "temperature_source_integrated": False,
        },
        "imd_capture": {
            "oracle": "DER26 AMS v2.6.27 / FW0.5.30",
            "pwm_input": "PA5/TIM2_CH1",
            "ok_hs_input": "PC5_active_high",
            "timer_clock_hz": 108_000_000,
            "timer_prescaler": 0,
            "timer_irq": 28,
            "timer_irq_priority": 5,
            "capture_mode": "CH1_rising_period+CH2_indirect_falling_high+slave_reset_TI1FP1",
            "capture_timeout_ms": 250,
            "task_period_ms": 100,
            "heartbeat_timeout_ms": 500,
            "status_frequency_step_hz": 10,
            "callback_error_behavior": "immediate_fail_closed_until_clean_capture",
            "capture_enable_failure": "soft_process_fault",
            "platform_config_failure": "fatal_fail_low",
            "bms_fail_low_on_not_ok": True,
            "heartbeat_after_fail_low": True,
            "target_physically_validated": False,
            "vehicle_authority_eligible": False,
        },
        "peripheral_state": {
            "can1_enabled": dts_enabled(dts, "can1:"),
            "spi6_enabled": dts_enabled(dts, "spi6:"),
            "adc1_enabled": dts_enabled(dts, "adc1:"),
            "adc2_enabled": dts_enabled(dts, "adc2:"),
            "timers2_enabled": dts_enabled(dts, "timers2:"),
            "timers3_enabled": dts_enabled(dts, "timers3:"),
            "timers4_enabled": dts_enabled(dts, "timers4:"),
            "timers5_enabled": dts_enabled(dts, "timers5:"),
            "iwdg_enabled": dts_enabled(dts, "iwdg:"),
            "authority_expected": False,
        },
        "artifacts": artifact_sizes,
    }

    args.output.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    args.output.write_text(
        json.dumps(
            manifest,
            indent=2,
            sort_keys=True,
        ) + "\n",
        encoding="utf-8",
    )

    print(f"WROTE: {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())