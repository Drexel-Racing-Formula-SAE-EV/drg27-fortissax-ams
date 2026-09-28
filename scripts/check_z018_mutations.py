#!/usr/bin/env python3
"""Source gate negative controls; each mutation must be rejected individually."""
from pathlib import Path
import sys, tempfile, shutil, contextlib, io
from check_z018_contract import source_check
r=Path(sys.argv[1]).resolve()
h='lib/ams_core/include/ams_core/ams_temp_image.h'
m='lib/ams_core/adbms/ams_adbms_monitor.c'
a='drivers/ams/adbms_monitor_zephyr.c'
p='lib/ams_core/adbms/ams_adbms_protocol.c'
cases=[(h,x,y) for x,y in [('AMS_TEMP_COUNT 24U','AMS_TEMP_COUNT 25U'),('AMS_TEMP_ALL_MASK 0x00FFFFFFUL','AMS_TEMP_ALL_MASK 0xFFFFFFFFUL'),('AMS_TEMP_STALE_MS 12000U','AMS_TEMP_STALE_MS 12001U'),('AMS_TEMP_MAX_MISSES 10U','AMS_TEMP_MAX_MISSES 11U'),('AMS_TEMP_JUMP_DECI_C 250U','AMS_TEMP_JUMP_DECI_C 251U'),('AMS_TEMP_RATE_DECI_C_PER_S 50U','AMS_TEMP_RATE_DECI_C_PER_S 51U')]]
cases += [(m,x,y) for x,y in [
 ('0x4CU+mux','0x4DU+mux'),('sensor/8U','sensor/7U'),('sensor%8U','sensor%7U'),
 ('comm[6]={0x68U,address,0x08U,data,0x19U,0xFFU}','comm[6]={0x60U,address,0x00U,data,0x19U,0xFFU}'),
 ('frame[13]={0}','frame[4]={0}'),('packet.data[0]!=0x67U','false'),
 ('packet.data[2]!=0x07U && packet.data[2]!=0x77U','false'),
 ('memcmp(packet.data,comm,6U)!=0','false'),
 ('*raw==INT16_MIN || *raw==(int16_t)-1','false'),
 ('mux<3U && result==AMS_ADBMS_RESULT_OK','mux<3U'),
 ('mux*8U+position','mux*7U+position'),
 ('captured[sensor]=now_ms+(uint32_t)((now-start)/1000U)','captured[sensor]=now_ms'),
 ('(position+1U)%8U','position'),
 ('9000U,AMS_ADBMS_CMD_RDRAXA','1000U,AMS_ADBMS_CMD_RDRAXA'),
 ('d->delta_mv>20','d->delta_mv>200'),('temporary[2]=0xB8U','temporary[2]=0x80U'),
 ('m->temperature.config_cleanup_required=true','m->temperature.config_cleanup_required=false'),
 ('!muted_cfga_matches_production(packet.data)','false'),
 ('m->initialized=false; m->acquisition_live=false; m->config_verified=false','m->initialized=true; m->acquisition_live=true; m->config_verified=true'),
 ('d->delta_mv<10 || d->pullup_delta_mv<10 || d->recovery_delta_mv>20','false'),
 ('return cleanup_snapshot(m,io)','return AMS_ADBMS_RESULT_OK'),
 ('d->valid=true; d->delta_mv=temp_delta_mv(d->baseline,d->secondary);','m->temperature.image.raw[sensor]=d->secondary; d->valid=true; d->delta_mv=temp_delta_mv(d->baseline,d->secondary);'),
]]
cases += [(a,x,y) for x,y in [
 ('k_current_get() != owner_thread','false'),
 ('ams_adbms_monitor_temperature(&monitor, &monitor_io, temp_now)','ams_adbms_monitor_temperature(&monitor, &monitor_io, now_ms)'),
 ('(uint32_t)(temp_now - aux2_last_ms) >= 250U','true'),
 ('(uint32_t)(temp_now - ow_last_ms) >= 2000U','true'),
 ('aux2_last_ms = temp_now','aux2_last_ms += 250U'),('ow_last_ms = temp_now','ow_last_ms += 2000U'),
 ('COPY(temperature)','COPY(cells)'),('AMS_ADBMS_SPI_STRING_B','AMS_ADBMS_SPI_STRING_A')]]
for name in ['z018_temp_validation','z018_aux2_validation','z018_therm_ow_validation']:
 cases.append((f'app/{name}.conf','CONFIG_AMS_BMS_AUTHORITY=n','CONFIG_AMS_BMS_AUTHORITY=y'))
 cases.append((f'app/{name}.conf','CONFIG_AMS_BALANCE_AUTHORITY=n','CONFIG_AMS_BALANCE_AUTHORITY=y'))
cases += [('app/z018_temp_validation.conf','CONFIG_AMS_Z018_TEMP_VALIDATION=y','CONFIG_AMS_Z018_TEMP_VALIDATION=n'),
 ('app/z018_temp_validation.conf','CONFIG_AMS_Z018_AUX2_DIAGNOSTIC=n','CONFIG_AMS_Z018_AUX2_DIAGNOSTIC=y'),
 ('app/Kconfig','config AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE\n    bool\n    default n','config AMS_CAP_TEMPERATURE_SAFETY_EVIDENCE\n    bool\n    default y'),
 ('lib/ams_core/adbms/ams_temp_image.c','7*(int32_t)im->filtered_deci_c[i]+v','6*(int32_t)im->filtered_deci_c[i]+v'),
 ('lib/ams_core/include/ams_core/ams_thermistor.h','10000.0f','4700.0f'),
 ('tests/unit/adbms_monitor/Makefile','adapter-z018-diag:','removed-diag:')]
for cmd in ['STCOMM','WRCOMM','RDCOMM','ADAX_GPIO1','ADAX2_GPIO1','ADAX_OW_UP_GPIO1']:
 import re
 old=re.search(r'\[AMS_ADBMS_CMD_'+cmd+r'\] = \{[^\n]+', (r/p).read_text()).group(0)
 cases.append((p,old,old.replace('0x','0xF',1)))
source_check(r)
with tempfile.TemporaryDirectory(prefix='z018-negative-') as temp:
 root=Path(temp)
 for name in ['app','lib','drivers','include']:
  shutil.copytree(r/name,root/name)
 shutil.copytree(r/'tests/unit/adbms_monitor',root/'tests/unit/adbms_monitor',ignore=shutil.ignore_patterns('*_test','*_test_asan','*_test_ubsan'))
 for file,old,new in cases:
  f=root/file;s=f.read_text()
  if old not in s:raise SystemExit('FAIL missing mutation anchor: '+old)
  f.write_text(s.replace(old,new,1));rejected=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):
    try:source_check(root)
    except SystemExit as e:rejected=e.code not in (None,0)
  finally:f.write_text(s)
  if not rejected:raise SystemExit('FAIL mutation survived: '+old)
print(f'PASS Z018 mutations: {len(cases)}/{len(cases)} rejected')
