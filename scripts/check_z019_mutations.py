#!/usr/bin/env python3
from pathlib import Path
import contextlib,io,shutil,sys,tempfile
from check_z019_contract import source_check
r=Path(sys.argv[1]).resolve();m='lib/ams_core/adbms/ams_adbms_monitor.c';a='drivers/ams/adbms_monitor_zephyr.c'
cases=[(m,x,y) for x,y in [
 ('ams_cell_image_init(&m->cells);','/* retained old cells */'),
 ('memset(&m->temperature.image, 0, sizeof(m->temperature.image));','/* retained old temperatures */'),
 ('m->temperature.mux_valid_mask = 0U;','m->temperature.mux_valid_mask = 7U;'),
 ('m->raw_fresh_mask = m->avg8_fresh_mask = m->iir_fresh_mask = 0U;','/* retained freshness */'),
 ('if (m->recovery.terminal) return','if (false) return'),
 ('if (m->recovery.continuity_lost) {','if (false) {'),
 ('next.cells.updated_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK','false'),
 ('next.cells.usable_mask != AMS_ADBMS_Z017_MONITORED_CELL_MASK','false'),
 ('result = audit_remote(&next, io);','result = AMS_ADBMS_RESULT_OK;'),
 ('preserve_recovery_history(&next, m);','/* discarded failure history */'),
 ('next.snapshot_cleanup_required |= m->snapshot_cleanup_required;','next.snapshot_cleanup_required = false;'),
 ('next.temperature.config_cleanup_required |= m->temperature.config_cleanup_required;','next.temperature.config_cleanup_required = false;'),
 ('memcmp(a.data, expected_a, 6U) != 0','false'),
 ('memcmp(b.data, expected_b, 6U) != 0','false'),
 ('expected_a[5] |= 0x10U','expected_a[5] |= 0U'),
 ('memcmp(pwm.data, zero, 6U) != 0','false'),
 ('memcmp(sid.data, m->recovery.sid, 6U) != 0','false'),
 ('!m->counter.known || m->snapshot_cleanup_required','false || m->snapshot_cleanup_required'),
]]
cases += [(a,x,y) for x,y in [
 ('!= AMS_ADBMS_RESULT_OK || was_pending','!= AMS_ADBMS_RESULT_OK'),
 ('cell_result != AMS_ADBMS_RESULT_OK || monitor.recovery.continuity_lost','cell_result != AMS_ADBMS_RESULT_OK'),
 ('COPY(recovery)','COPY(cells)'),('k_current_get() != owner_thread','false')]]
cases += [('app/z019_recovery_validation.conf','CONFIG_AMS_BMS_AUTHORITY=n','CONFIG_AMS_BMS_AUTHORITY=y'),('app/z019_recovery_validation.conf','CONFIG_AMS_BALANCE_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=y'),('app/z019_recovery_validation.conf','CONFIG_AMS_Z018_TEMP_VALIDATION=y','CONFIG_AMS_Z018_TEMP_VALIDATION=n')]
source_check(r)
with tempfile.TemporaryDirectory(prefix='z019-mutations-') as td:
 root=Path(td)
 for name in ['app','lib','drivers','include']:
  shutil.copytree(r/name,root/name)
 shutil.copytree(r/'tests/unit/adbms_monitor',root/'tests/unit/adbms_monitor',ignore=shutil.ignore_patterns('*_test','*_test_asan','*_test_ubsan'))
 for file,old,new in cases:
  p=root/file;s=p.read_text();assert old in s,old
  # Apply the selected unsafe rule everywhere it occurs (e.g. both SID checks).
  p.write_text(s.replace(old,new));rejected=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):
    try:source_check(root)
    except SystemExit as e:rejected=e.code not in (None,0)
  finally:p.write_text(s)
  if not rejected:raise SystemExit('FAIL mutation survived: '+old)
print(f'PASS Z019 mutations: {len(cases)}/{len(cases)} rejected')
