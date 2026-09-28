#!/usr/bin/env python3
"""Negative controls for the Z017 String-B monitor/acquisition contract."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import shutil
import sys
import tempfile
from pathlib import Path

repo = Path(sys.argv[1]).resolve()
cases = [
 ('lib/ams_core/include/ams_core/ams_adbms_protocol.h','AMS_ADBMS_Z017_STRING_B 1U','AMS_ADBMS_Z017_STRING_B 0U'),
 ('lib/ams_core/include/ams_core/ams_adbms_protocol.h','AMS_ADBMS_Z017_IC_COUNT 1U','AMS_ADBMS_Z017_IC_COUNT 5U'),
 ('lib/ams_core/include/ams_core/ams_adbms_protocol.h','AMS_ADBMS_Z017_MONITORED_CELL_COUNT 15U','AMS_ADBMS_Z017_MONITORED_CELL_COUNT 16U'),
 ('app/z017_cell_validation.conf','CONFIG_AMS_BALANCE_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=y'),
 ('app/Kconfig','depends on !AMS_Z016_LINK_PROBE','depends on AMS_Z016_LINK_PROBE'),
 ('lib/ams_core/adbms/ams_adbms_protocol.c','[AMS_ADBMS_CMD_ADCV_Z017]    = {0x03U, 0xE0U','[AMS_ADBMS_CMD_ADCV_Z017]    = {0x03U, 0x60U'),
 ('lib/ams_core/include/ams_core/ams_adbms_monitor.h','AMS_ADBMS_Z017_MAX_POST_ATTEMPTS 2U','AMS_ADBMS_Z017_MAX_POST_ATTEMPTS 3U'),
 ('lib/ams_core/include/ams_core/ams_adbms_monitor.h','AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS 2U','AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS 3U'),
 ('lib/ams_core/include/ams_core/ams_adbms_protocol.h','    AMS_ADBMS_CMD_MUTE,','    AMS_ADBMS_CMD_MUTE,\n    AMS_ADBMS_CMD_UNMUTE,'),
 ('lib/ams_core/adbms/ams_adbms_protocol.c','[AMS_ADBMS_CMD_MUTE]         = {0x00U, 0x28U','[AMS_ADBMS_CMD_MUTE]         = {0x00U, 0x29U'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','        m->snapshot_cleanup_required = true;','        /* cleanup obligation removed */'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','    if (m->snapshot_cleanup_required) {','    if (false) {'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','        result = startup_balance_inhibit(m, io);','        result = AMS_ADBMS_RESULT_OK; /* inhibit removed */'),
 ('lib/ams_core/adbms/ams_cell_image.c','    image->iir_usable_mask = 0U;\n\n    if (!complete_epoch','    image->iir_usable_mask = usable;\n\n    if (!complete_epoch'),
 ('drivers/ams/adbms_monitor_zephyr.c','AMS_ADBMS_SPI_STRING_B,','AMS_ADBMS_SPI_STRING_A,'),
 ('include/ams_platform/adbms_monitor.h','/* Copied diagnostic view only. No raw opcode, direction, CS, SPI buffer, or','ams_adbms_command_t leaked_opcode;\n/* Copied diagnostic view only. No raw opcode, direction, CS, SPI buffer, or'),
 ('app/src/ams_threads.c','                ams_adbms_monitor_platform_step((uint32_t)start_ms);','                ams_adbms_monitor_platform_step((uint32_t)start_ms);\n                watchdog_heartbeat_kick(AMS_WATCHDOG_HEARTBEAT_ADBMS);'),
 ('drivers/ams/adbms_monitor_zephyr.c','static ams_adbms_monitor_t monitor;','ams_measurement_publish();\nstatic ams_adbms_monitor_t monitor;'),
 ('lib/ams_core/include/ams_core/ams_adbms_monitor.h','post_failed_stage_mask','post_failed_mask'),
 ('app/z017_cell_validation.conf','CONFIG_AMS_Z016_LINK_PROBE=n','CONFIG_AMS_Z016_LINK_PROBE=y'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','        result = run_post(m, io);','        result = AMS_ADBMS_RESULT_OK; /* POST omitted */'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','        result = verify_config(m, io);\n    }\n    if (result == AMS_ADBMS_RESULT_OK) {\n        result = startup_balance_inhibit(m, io);','        result = AMS_ADBMS_RESULT_OK; /* final readback omitted */\n    }\n    if (result == AMS_ADBMS_RESULT_OK) {\n        result = startup_balance_inhibit(m, io);'),
 ('lib/ams_core/include/ams_core/ams_adbms_protocol.h','    AMS_ADBMS_CMD_MUTE,','    AMS_ADBMS_CMD_MUTE,\n    AMS_ADBMS_CMD_WRCOMM,'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','ams_cell_image_apply_avg8(&m->cells, m->avg8_codes,','ams_cell_image_apply_avg8(&m->cells, m->raw_codes,'),
 ('lib/ams_core/adbms/ams_adbms_monitor.c','    monitor->state = AMS_ADBMS_MONITOR_UNINITIALIZED;','    monitor->state = AMS_ADBMS_MONITOR_UNINITIALIZED; monitor->physical_validated = true;'),
 ('app/src/ams_threads.c','            do {\n                next_release_ms += thread->period_ms;\n            } while (complete_ms >= next_release_ms);','            next_release_ms += thread->period_ms; /* catch-up burst regression */'),
]

with tempfile.TemporaryDirectory(prefix='z017-mutations-') as d:
    root = Path(d) / 'repo'
    root.mkdir()
    # The contract reads only these source/profile trees. Keep the fixture tiny;
    # release evidence/oracle payloads must not dominate a negative-control run.
    for name in ('app', 'lib', 'drivers', 'include'):
        shutil.copytree(repo / name, root / name,
                        ignore=shutil.ignore_patterns('build', '__pycache__', '.git',
                                                      'monitor_test', 'adapter_test',
                                                      '*.o', '*.plist'))
    (root / 'tests/unit').mkdir(parents=True)
    shutil.copytree(repo / 'tests/unit/adbms_monitor', root / 'tests/unit/adbms_monitor',
                    ignore=shutil.ignore_patterns('monitor_test', 'monitor_test_*',
                                                  'adapter_test', '*.o', '*.plist'))
    (root / 'scripts').mkdir()
    shutil.copy2(repo / 'scripts/check_z017_contract.py',
                 root / 'scripts/check_z017_contract.py')

    spec = importlib.util.spec_from_file_location('z017_contract',
                                                   root / 'scripts/check_z017_contract.py')
    if spec is None or spec.loader is None:
        raise SystemExit('FAIL: unable to load Z017 contract checker')
    contract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(contract)

    contract.source_check(root)

    for file, old, new in cases:
        p = root / file
        original = p.read_text()
        if old not in original:
            raise SystemExit(f'FAIL: mutation anchor missing: {file}: {old}')
        p.write_text(original.replace(old, new, 1))
        survived = False
        try:
            # Expected contract failures print diagnostics. Suppress them here;
            # this suite reports only surviving mutations or the final count.
            with contextlib.redirect_stdout(io.StringIO()):
                try:
                    contract.source_check(root)
                    survived = True
                except SystemExit as exc:
                    if exc.code in (None, 0):
                        survived = True
        finally:
            p.write_text(original)
        if survived:
            raise SystemExit('FAIL: Z017 mutation survived: ' + file + ': ' + old)

print(f'PASS: {len(cases)} Z017 unsafe profile/protocol/ownership/coherence/authority changes rejected')
