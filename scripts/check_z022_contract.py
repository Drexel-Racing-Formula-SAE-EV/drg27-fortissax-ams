#!/usr/bin/env python3
from pathlib import Path
import sys
from check_z017_contract import elf_symbols

def source_check(r):
 def require(ok,msg):
  if not ok:raise SystemExit('FAIL Z022: '+msg)
 cfg=(r/'app/z022_measurement_validation.conf').read_text().splitlines()
 for k in ['AMS_Z022_MEASUREMENT_VALIDATION','AMS_Z020_BALANCE_DISABLED_VALIDATION','AMS_Z018_TEMP_VALIDATION','AMS_Z019_RECOVERY_VALIDATION']:
  require('CONFIG_'+k+'=y' in cfg,'dependency '+k)
 for k in ['AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY']:
  require('CONFIG_'+k+'=n' in cfg and 'CONFIG_'+k+'=y' not in cfg,'authority '+k)
 c=(r/'lib/ams_core/measurement/ams_measurement_pipeline.c').read_text()
 b=c[c.index('bool ams_pipeline_boundary('):c.index('ams_sequence_t ams_pipeline_publish_single(')]
 require(b.index('p->ops.lock')<b.index('p->ops.now_ms')<b.index('ams_current_window_rotate')<b.index('p->ops.unlock'),'lock/time/rotate order')
 for t in ['atomic_store(&p->dropped_current,true)','atomic_exchange(&p->dropped_current,false)','s->cell_age_ms[seg][i]=UINT32_MAX','if(c->iir_ready)','if (coherent && c && t)','p->boundary_ready=false;','ams_measurement_store_begin_write','ams_measurement_store_publish']:
  require(t in c,'missing '+t)
 publication=c[c.index('ams_sequence_t ams_pipeline_publish_single('):c.index('bool ams_pipeline_estimator_eligible(')]
 require('AMS_MEAS_VALID_VOLTAGE' not in publication and 'AMS_MEAS_VALID_TEMPERATURE' not in publication,'single SMB fabricated full pack')
 a=(r/'drivers/ams/measurement_pipeline_zephyr.c').read_text()
 for t in ['k_mutex_lock(&current_lock,K_MSEC(2))','k_current_get()!=current_owner','k_current_get()!=voltage_owner','!m.recovery.pending','!m.recovery.terminal','!m.recovery.continuity_lost','!m.snapshot_cleanup_required','!m.temperature.config_cleanup_required']:
  require(t in a,'adapter invariant '+t)
 require('watchdog_heartbeat' not in a,'premature safety evidence')
 for t in ['current_fault_update(&current_fault,CURRENT_FAULT_MODE_PRECHARGE','ams_segment_consumer_step(&consumer','coherent&&m.balance_mute_verified&&m.balance_durable_zero_verified']:
  require(t in a,'missing completed workload '+t)
 owner=(r/'drivers/ams/adbms_monitor_zephyr.c').read_text();step=owner[owner.index('void ams_adbms_monitor_platform_step('):]
 require(step.index('ams_z022_voltage_boundary()')<step.index('ams_adbms_monitor_temperature('),'boundary after temperatures')
 threads=(r/'app/src/ams_threads.c').read_text()
 for t in ['AMS_THREAD_CURRENT]) { ams_z022_current_step();','AMS_THREAD_ADBMS]) { ams_z022_begin_release();','AMS_THREAD_ADBMS]) { ams_z022_publish_release();']:
  require(t in threads,'thread dispatch '+t)
 require('AMS_THREAD_ESTIMATOR]) { ams_z022_estimator_step();' in threads,'estimator dispatch')
 consumer=(r/'lib/ams_core/estimator/ams_segment_consumer.c').read_text()
 for t in ['s->sequence==c->sequence','now-s->publication_tick)>100U','s->current.total_invalid_sample_count==c->invalid_total','AMS_MEAS_BALANCE_RECOVERED','s->cell_usable_mask[seg]==AMS_CELL_IMAGE_MONITORED_MASK','ams_ekf_step_gated(e,current,voltage,temp,dt,false,&result)','ams_ekf_acquisition_observe']:
  require(t in consumer,'consumer invariant '+t)
 print('PASS Z022 source contract')
if __name__=='__main__':
 r=Path(sys.argv[1]).resolve();source_check(r)
 if len(sys.argv)>2:
  b=Path(sys.argv[2]).resolve();cfg=(b/'zephyr/.config').read_text().splitlines();active='CONFIG_AMS_Z022_MEASUREMENT_VALIDATION=y' in cfg
  symbols=elf_symbols(b/'zephyr/zephyr.elf')
  for name in ['ams_z022_current_step','ams_z022_publish_release','ams_pipeline_boundary','ams_pipeline_publish_single','ams_z022_estimator_step','ams_segment_consumer_step']:
   if (name in symbols)!=active:raise SystemExit('FAIL Z022 ELF '+name)
  if active and 'CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION=y' not in cfg:raise SystemExit('FAIL Z022 dependency')
  print('PASS Z022 target contract')
