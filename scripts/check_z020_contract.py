#!/usr/bin/env python3
"""Current-board Z020: diagnostic planner, no balancing authority."""
from pathlib import Path
import sys
from check_z019_contract import target_check as inherited_target

def require(ok, msg):
    if not ok: raise SystemExit("FAIL Z020: " + msg)
from check_z017_contract import elf_symbols

def source_check(r):
    k=(r/'app/Kconfig').read_text()
    block=k.split('config AMS_Z020_BALANCE_DISABLED_VALIDATION\n')[1].split('\nconfig ')[0].split('endmenu')[0]
    for t in ['depends on AMS_Z019_RECOVERY_VALIDATION','depends on !AMS_BALANCE_AUTHORITY && !AMS_BMS_AUTHORITY','default n']:
        require(t in block,'Z020 dependency '+t)
    cfg=(r/'app/z020_balance_disabled_validation.conf').read_text().splitlines()
    for t in ['AMS_Z020_BALANCE_DISABLED_VALIDATION','AMS_Z019_RECOVERY_VALIDATION','AMS_Z018_TEMP_VALIDATION']:
        require('CONFIG_'+t+'=y' in cfg,'Z020 profile '+t)
    for t in ['AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY','AMS_Z017_CELL_VALIDATION','AMS_Z016_LINK_PROBE']:
        require('CONFIG_'+t+'=n' in cfg and 'CONFIG_'+t+'=y' not in cfg,'Z020 forbidden '+t)
    h=(r/'lib/ams_core/include/ams_core/ams_balance_shadow.h').read_text()
    for t in ['AMS_BALANCE_SHADOW_START_MV 4100U','AMS_BALANCE_SHADOW_DELTA_MV 20U','AMS_BALANCE_SHADOW_MAX_CELLS 4U']:
        require(t in h,'planner constant '+t)
    c=(r/'lib/ams_core/adbms/ams_balance_shadow.c').read_text()
    for t in ['memset(out, 0, sizeof(*out))','!owner_ready','cells->usable_mask != AMS_CELL_IMAGE_MONITORED_MASK','!cells->raw_valid[i]', 'mv < AMS_CELL_IMAGE_VALID_MIN_MV','mv > AMS_CELL_IMAGE_VALID_MAX_MV','(uint32_t)(now_ms - cells->last_update_ms[i]) > AMS_CELL_IMAGE_STALE_TIMEOUT_MS','cells->consecutive_misses[i] > AMS_CELL_IMAGE_MAX_CONSEC_MISSES','mv >= AMS_BALANCE_SHADOW_START_MV','(uint16_t)(mv - minimum) > AMS_BALANCE_SHADOW_DELTA_MV','count < AMS_BALANCE_SHADOW_MAX_CELLS']:
        require(t in c,'planner invariant '+t)
    for t in ['write_b','UNMUTE','WRCFG','WRPWM','ams_platform']:
        require(t not in c,'shadow transport leakage '+t)
    a=(r/'drivers/ams/adbms_monitor_zephyr.c').read_text()
    for t in ['core.config_verified && core.balance_mute_verified','core.balance_durable_zero_verified && core.acquisition_live','core.state == AMS_ADBMS_MONITOR_READY','!core.recovery.pending && !core.recovery.terminal','!core.recovery.continuity_lost && !core.snapshot_cleanup_required','!core.temperature.config_cleanup_required','shadow_now_ms, &next.balance_shadow']:
        require(t in a,'shadow readiness '+t)
    require(a.count('ams_balance_shadow_evaluate(')==1,'shadow call count')
    print('PASS Z020 disabled-balancing contract')

def target_check(r,b):
    inherited_target(r,b)
    cfg=(b/'zephyr/.config').read_text().splitlines()
    active='CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION=y' in cfg
    require(not active or 'CONFIG_AMS_Z019_RECOVERY_VALIDATION=y' in cfg,'Z020 without recovery')
    symbols=elf_symbols(b/'zephyr/zephyr.elf')
    require(('ams_balance_shadow_evaluate' in symbols)==active,'Z020 shadow ELF profile inclusion')
    print('PASS Z020 target contract')
if __name__=='__main__':
    r=Path(sys.argv[1]).resolve();source_check(r)
    if len(sys.argv)>2:target_check(r,Path(sys.argv[2]).resolve())
