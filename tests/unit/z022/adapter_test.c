#include <ams_platform/measurement_pipeline.h>
#include <ams_platform/adbms_monitor.h>
#include <ams_platform/current_adc.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t fake_now;uintptr_t fake_thread=1;bool fake_isr,fake_locked;
static unsigned reads;static bool failed,pending;
int ams_current_adc_read_pair(ams_current_adc_pair_t *p){assert(!fake_locked);reads++;memset(p,0,sizeof(*p));p->high_count=p->low_count=2048;p->complete=p->high_fresh=p->low_fresh=!failed;return failed?-1:0;}
bool ams_adbms_monitor_platform_snapshot(ams_adbms_monitor_platform_snapshot_t *m){memset(m,0,sizeof(*m));m->initialized=m->config_verified=m->acquisition_live=true;m->state=AMS_ADBMS_MONITOR_PLATFORM_READY;m->recovery.pending=pending;m->cells.usable_mask=0x7fff;m->temperature.image.usable_mask=m->temperature.image.valid_mask=0xffffff;for(unsigned i=0;i<15;i++){m->cells.raw_valid[i]=true;m->cells.raw_mv[i]=4000;m->cells.last_update_ms[i]=fake_now;}for(unsigned i=0;i<24;i++){m->temperature.image.deci_c[i]=250;m->temperature.image.last_update_ms[i]=fake_now;}return true;}
int main(void){
 assert(ams_z022_init());assert(!ams_z022_init());ams_z022_current_step();assert(reads==1);
 fake_thread=2;ams_z022_current_step();assert(reads==1);ams_z022_begin_release();fake_now=20;ams_z022_voltage_boundary();fake_now=25;ams_z022_publish_release();
 ams_measurement_snapshot_t s;assert(ams_z022_copy_latest(&s));assert(s.voltage_complete_tick==20&&s.publication_tick==25&&s.cell_usable_mask[0]==0x7fff&&s.cell_usable_mask[1]==0);
 unsigned sequence=s.sequence;ams_z022_publish_release();assert(ams_z022_copy_latest(&s)&&s.sequence==sequence);
 pending=true;ams_z022_begin_release();fake_now=40;ams_z022_publish_release();assert(ams_z022_copy_latest(&s)&&!s.cell_usable_mask[0]);
 fake_thread=1;failed=true;ams_z022_current_step();assert(reads==2);
 fake_thread=2;pending=false;fake_now=60;ams_z022_begin_release();ams_z022_publish_release();assert(ams_z022_copy_latest(&s)&&!(s.validity_flags&AMS_MEAS_VALID_CURRENT));
 fake_thread=1;fake_isr=true;ams_z022_current_step();assert(reads==2&&!ams_z022_copy_latest(&s));fake_isr=false;
 ams_z022_diagnostics_t d;assert(ams_z022_copy_diagnostics(&d));
 assert(d.current_completions==2&&d.current_fault.sensor_invalid_ms>0&&d.current_fault.mode==CURRENT_FAULT_MODE_PRECHARGE);
 fake_thread=3;ams_z022_estimator_step();assert(ams_z022_copy_diagnostics(&d)&&d.estimator_epochs==1&&!d.estimator_valid_mask);
 ams_z022_estimator_step();assert(ams_z022_copy_diagnostics(&d)&&d.estimator_epochs==1);
 puts("PASS Z022 production adapter: current/voltage ownership, boundary before temperature publication, failure withdrawal, no duplicate publish");
 return 0;
}
