#!/usr/bin/env python3
from pathlib import Path
import contextlib,io,shutil,sys,tempfile
from check_z020_contract import source_check
r=Path(sys.argv[1]).resolve()
c='lib/ams_core/adbms/ams_balance_shadow.c';h='lib/ams_core/include/ams_core/ams_balance_shadow.h';a='drivers/ams/adbms_monitor_zephyr.c';p='app/z020_balance_disabled_validation.conf'
cases=[(c,x,'false') for x in ['!owner_ready','cells->usable_mask != AMS_CELL_IMAGE_MONITORED_MASK','!cells->raw_valid[i]','mv < AMS_CELL_IMAGE_VALID_MIN_MV','mv > AMS_CELL_IMAGE_VALID_MAX_MV','(uint32_t)(now_ms - cells->last_update_ms[i]) > AMS_CELL_IMAGE_STALE_TIMEOUT_MS','cells->consecutive_misses[i] > AMS_CELL_IMAGE_MAX_CONSEC_MISSES']]
cases += [(c,'memset(out, 0, sizeof(*out));','/* retain stale plan */'),(c,'> AMS_BALANCE_SHADOW_DELTA_MV','>= AMS_BALANCE_SHADOW_DELTA_MV'),(c,'count < AMS_BALANCE_SHADOW_MAX_CELLS','true')]
cases += [(h,x,y) for x,y in [('START_MV 4100U','START_MV 4000U'),('DELTA_MV 20U','DELTA_MV 10U'),('MAX_CELLS 4U','MAX_CELLS 15U')]]
cases += [(a,x,'true') for x in ['core.balance_mute_verified','core.balance_durable_zero_verified','!core.recovery.pending','!core.recovery.terminal','!core.recovery.continuity_lost','!core.snapshot_cleanup_required','!core.temperature.config_cleanup_required']]
cases += [(p,'CONFIG_'+x+'=n','CONFIG_'+x+'=y') for x in ['AMS_BALANCE_AUTHORITY','AMS_BMS_AUTHORITY']]
cases += [(p,'CONFIG_AMS_Z018_TEMP_VALIDATION=y','CONFIG_AMS_Z018_TEMP_VALIDATION=n'),(p,'CONFIG_AMS_Z019_RECOVERY_VALIDATION=y','CONFIG_AMS_Z019_RECOVERY_VALIDATION=n')]
source_check(r)
with tempfile.TemporaryDirectory(prefix='z020-mutations-') as td:
 d=Path(td)
 for n in ['app','lib','drivers']:shutil.copytree(r/n,d/n)
 for f,old,new in cases:
  file=d/f;s=file.read_text();assert old in s,old;file.write_text(s.replace(old,new));rejected=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):
    try:source_check(d)
    except SystemExit as e:rejected=e.code not in (None,0)
  finally:file.write_text(s)
  if not rejected:raise SystemExit('FAIL mutation survived '+old)
print(f'PASS Z020 mutations: {len(cases)}/{len(cases)} rejected')
