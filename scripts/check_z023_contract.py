#!/usr/bin/env python3
from pathlib import Path
import sys
from check_z017_contract import elf_symbols

REQUIRED = {
 'lib/ams_core/faults/ams_segment_fault.c': [
  's->read_streak<3U', 'max>=4250U', 'min<=2300U', 'max>=4200U', 'min<=2500U',
  'max>=4180U', 'min<=2800U', 'max>=4150U', 'min<=3000U',
  'v>=4220U', 'v<=4180U', 'v<=2980U', 'v>=3020U',
  't->startup_scan_complete', 'max>=600', 'max>=650',
  'pending+100U', 's->pending_ms>=2000U', 'max>=500', 'max>=450', 'min<=0'],
 'lib/ams_core/faults/ams_supervision.c': [
  'r->sequence==UINT64_MAX', 'r->sequence<=s->actor[actor].sequence',
  '(uint32_t)(now-r->completed_ms)>timeout[actor]',
  's->sticky_faults|=r->history', 'r->sequence>s->consumed[a]',
  's->heartbeat_ms[a]=r->completed_ms', 's->inhibit=true',
  'timeout[AMS_SUP_ACTORS]={200U,3000U,3000U}',
  '(uint32_t)(now-s->start_ms)>=3000U',
  's->positions&(1U<<position)', '(converted&failed)',
  '(uint32_t)(generation-s->generation)>=0x80000000U'],
 'drivers/ams/supervision_zephyr.c': [
  'k_current_get()==owners[actor]', 'k_current_get()!=supervisor',
  '!ams_z023_owner(AMS_SUP_ADBMS) || !pending',
  'if(!published)pending_faults|=AMS_SUP_PUBLICATION',
  'm->snapshot_cleanup_required || m->temperature.config_cleanup_required',
  'm->recovery.pending || m->recovery.terminal || m->recovery.continuity_lost',
  'temp_complete=false', 'ams_supervision_evaluate(&decision,now);*out=decision'],
 'drivers/ams/measurement_pipeline_zephyr.c': [
  'if(!ams_z023_owner(AMS_SUP_CURRENT))return',
  'if(!ams_z023_owner(AMS_SUP_ADBMS))return',
  'ams_z023_current_complete(faults)', 'ams_z023_publication_complete(published!=0U)'],
 'drivers/ams/adbms_monitor_zephyr.c': [
  'if (!ams_z023_owner(AMS_SUP_ADBMS)) return false',
  'supervision_work = !monitor.recovery.terminal',
  'ams_z023_monitor_result(&next)', 'ams_z023_temperature_result(monitor.recovery.generation,position'],
 'app/src/ams_threads.c': [
  'ams_z023_bind(&current_thread, &adbms_thread, &safety_thread)',
  'ams_z023_safety_cycle();'],
 'app/src/ams_z023_safety.c': [
  'if(!ams_z023_poll(&decision) || decision.inhibit)ams_bms_ok_force_low_direct();'],
}
def source_check(root):
 def require(ok,msg):
  if not ok:raise SystemExit('FAIL Z023: '+msg)
 for path,tokens in REQUIRED.items():
  source=(root/path).read_text()
  for token in tokens:require(token in source,path+': '+token)
 cfg=(root/'app/z023_supervision_validation.conf').read_text().splitlines()
 for name in ['Z018_TEMP','Z019_RECOVERY','Z020_BALANCE_DISABLED','Z022_MEASUREMENT','Z023_SUPERVISION']:
  require('CONFIG_AMS_'+name+'_VALIDATION=y' in cfg,'dependency '+name)
 for name in ['BMS_AUTHORITY','BALANCE_AUTHORITY']:
  require('CONFIG_AMS_'+name+'=n' in cfg and 'CONFIG_AMS_'+name+'=y' not in cfg,'authority '+name)
 threads=(root/'app/src/ams_threads.c').read_text()
 for name in ['CURRENT','ADBMS','TEMPERATURE']:
  require('BUILD_ASSERT(!IS_ENABLED(CONFIG_AMS_CAP_'+name+'_SAFETY_EVIDENCE)' in threads,'evidence '+name)
 require(threads.index('ams_z023_bind(&current_thread')<threads.index('start_thread_if_enabled(AMS_THREAD_SAFETY);'),'bind before release')
 safety=threads[threads.index('static void safety_supervisor_thread('):]
 require(safety.index('ams_z023_safety_cycle();')<safety.index('heartbeat_snapshot = watchdog_heartbeat_snapshot_get();'),'supervision before watchdog evaluation')
 core=(root/'lib/ams_core/faults/ams_supervision.c').read_text()
 require(core.index('s->sticky_faults|=r->history')<core.index('s->consumed[a]=r->sequence'),'fault before progress')
 adapter=(root/'drivers/ams/supervision_zephyr.c').read_text()
 require('watchdog_heartbeat_kick' not in adapter,'unqualified heartbeat credit')
 print('PASS Z023 shadow source contract')

if __name__=='__main__':
 root=Path(sys.argv[1]).resolve();source_check(root)
 if len(sys.argv)>2:
  build=Path(sys.argv[2]);cfg=(build/'zephyr/.config').read_text().splitlines()
  active='CONFIG_AMS_Z023_SUPERVISION_VALIDATION=y' in cfg
  symbols=elf_symbols(build/'zephyr/zephyr.elf')
  for name in ['ams_z023_bind','ams_z023_poll','ams_z023_monitor_result','ams_z023_current_complete','ams_z023_safety_cycle']:
   if (name in symbols)!=active:raise SystemExit('FAIL Z023 ELF '+name)
  if active:
   for name in ['Z022_MEASUREMENT_VALIDATION','Z018_TEMP_VALIDATION']:
    if 'CONFIG_AMS_'+name+'=y' not in cfg:raise SystemExit('FAIL Z023 target dependency')
   for name in ['BMS_AUTHORITY','BALANCE_AUTHORITY','CAP_CURRENT_SAFETY_EVIDENCE','CAP_ADBMS_SAFETY_EVIDENCE','CAP_TEMPERATURE_SAFETY_EVIDENCE']:
    if 'CONFIG_AMS_'+name+'=y' in cfg:raise SystemExit('FAIL Z023 forbidden '+name)
  print('PASS Z023 target contract')
