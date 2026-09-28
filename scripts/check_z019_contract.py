#!/usr/bin/env python3
"""Z019 recovery lifecycle, source containment and target profile gate."""
from pathlib import Path
import sys
from check_z017_contract import elf_symbols

def require(ok,msg):
    if not ok:raise SystemExit('FAIL Z019: '+msg)

def source_check(r):
    m=(r/'lib/ams_core/adbms/ams_adbms_monitor.c').read_text()
    a=(r/'drivers/ams/adbms_monitor_zephyr.c').read_text()
    k=(r/'app/Kconfig').read_text()
    cfg=(r/'app/z019_recovery_validation.conf').read_text()
    def tokens(s,seq):
        for tok in seq:require(tok in s,'missing invariant '+tok)
    tokens(cfg,['CONFIG_AMS_Z019_RECOVERY_VALIDATION=y','CONFIG_AMS_Z018_TEMP_VALIDATION=y','CONFIG_AMS_Z017_CELL_VALIDATION=n','CONFIG_AMS_Z016_LINK_PROBE=n','CONFIG_AMS_BMS_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=n'])
    block=k.split('config AMS_Z019_RECOVERY_VALIDATION\n')[1].split('\nconfig ')[0]
    tokens(block,['depends on AMS_Z018_TEMP_VALIDATION','default n'])
    require('select AMS_BMS' not in block and 'select AMS_CAP' not in block,'authority promotion')
    withdraw=m[m.index('static void withdraw_images('):m.index('void ams_adbms_monitor_interrupt(')]
    tokens(withdraw,['ams_cell_image_init(&m->cells)','memset(&m->temperature.image, 0, sizeof(m->temperature.image))','m->temperature.mux_valid_mask = 0U','m->raw_fresh_mask = m->avg8_fresh_mask = m->iir_fresh_mask = 0U','m->acquisition_live = false','m->config_verified = false','invalidate_session(m, true)'])
    require('snapshot_cleanup_required = false' not in withdraw,'lost remote SNAP debt')
    audit=m[m.index('static ams_adbms_result_t audit_remote('):m.index('static uint32_t sat_add(')]
    tokens(audit,['!m->counter.known','m->snapshot_cleanup_required','m->temperature.config_cleanup_required','memcmp(sid.data, m->recovery.sid, 6U) != 0','expected_a[5] |= 0x10U','memcmp(a.data, expected_a, 6U) != 0','memcmp(b.data, expected_b, 6U) != 0','AMS_ADBMS_CMD_RDPWMA','AMS_ADBMS_CMD_RDPWMB','memcmp(pwm.data, zero, 6U) != 0'])
    recovery=m[m.index('ams_adbms_result_t ams_adbms_monitor_recovery_step('):]
    tokens(recovery,['if (m->recovery.terminal) return','m->recovery.continuity_lost','sat_inc(&m->recovery.attempt_count)','result = cleanup_snapshot(m, io)','memcmp(sid.data, m->recovery.sid, 6U) != 0','ams_adbms_monitor_initialize(&next, io)','ams_adbms_monitor_acquire(&next, io, now_ms)','next.cells.updated_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK','next.cells.usable_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK','audit_remote(&next, io)','preserve_recovery_history(&next, m)','next.snapshot_cleanup_required |= m->snapshot_cleanup_required','next.temperature.config_cleanup_required |= m->temperature.config_cleanup_required','m->recovery.terminal = true','withdraw_images(m)'])
    require(recovery.index('cleanup_snapshot')<recovery.index('memcmp(sid.data')<recovery.index('ams_adbms_monitor_initialize'),'cleanup/identity-before-mutation ordering')
    require(recovery.index('ams_adbms_monitor_acquire')<recovery.index('audit_remote(&next')<recovery.index('*m = next'),'candidate committed before qualification')
    tokens(a,['CONFIG_AMS_Z019_RECOVERY_VALIDATION','bool was_pending = monitor.recovery.pending','!= AMS_ADBMS_RESULT_OK || was_pending','cell_result != AMS_ADBMS_RESULT_OK || monitor.recovery.continuity_lost','ams_adbms_monitor_interrupt(&monitor','k_current_get() != owner_thread','COPY(recovery)'])
    tokens((r/'tests/unit/adbms_monitor/Makefile').read_text(),['adapter-z019:'])
    print('PASS Z019 source contract')

def target_check(r,b):
    cfg=(b/'zephyr/.config').read_text().splitlines()
    en=lambda s:'CONFIG_'+s+'=y' in cfg
    active=en('AMS_Z019_RECOVERY_VALIDATION')
    require(not active or en('AMS_Z018_TEMP_VALIDATION'),'Z019 without Z018')
    for s in ['AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY','AMS_CAP_ADBMS_SAFETY_EVIDENCE','AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE']:
        require(not en(s),'forbidden authority '+s)
    symbols=elf_symbols(b/'zephyr/zephyr.elf')
    for s in ['ams_adbms_monitor_recovery_step','ams_adbms_monitor_interrupt']:
        require((s in symbols)==active,'ELF profile inclusion '+s)
    print('PASS Z019 target contract')
if __name__=='__main__':
    r=Path(sys.argv[1]).resolve();source_check(r)
    if len(sys.argv)>2:target_check(r,Path(sys.argv[2]).resolve())
