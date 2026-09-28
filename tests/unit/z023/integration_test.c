#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#include <ams_platform/measurement_pipeline.h>
#include <ams_platform/current_adc.h>
#include "ams_z023_safety.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t fake_now;uintptr_t fake_thread=1;
bool fake_isr,fake_locked,fake_mutex_failure;
static unsigned fail_low_calls,adc_calls;
static bool adc_failure;
static ams_adbms_monitor_platform_snapshot_t monitor_image;
void ams_bms_ok_force_low_direct(void){assert(!fake_locked);fail_low_calls++;}
int ams_current_adc_read_pair(ams_current_adc_pair_t *p)
{
 assert(!fake_locked);adc_calls++;memset(p,0,sizeof(*p));
 p->high_count=p->low_count=2048;p->complete=p->high_fresh=p->low_fresh=!adc_failure;
 return adc_failure?-1:0;
}
bool ams_adbms_monitor_platform_snapshot(ams_adbms_monitor_platform_snapshot_t *m)
{*m=monitor_image;return true;}
/* White-box fault injection only: inspect/pin the real store without creating
 * a production accessor that would expose writer ownership. */
#include "../../../drivers/ams/measurement_pipeline_zephyr.c"
static ams_supervision_t safety(void)
{
 fake_thread=3;unsigned before=fail_low_calls;ams_z023_safety_cycle();
 assert(fail_low_calls==before+1);ams_supervision_t d;assert(ams_z023_copy(&d));return d;
}
static void healthy(void)
{
 memset(&monitor_image,0,sizeof(monitor_image));
 monitor_image.initialized=monitor_image.config_verified=monitor_image.acquisition_live=true;
 monitor_image.balance_mute_verified=monitor_image.balance_durable_zero_verified=true;
 monitor_image.state=AMS_ADBMS_MONITOR_PLATFORM_READY;monitor_image.attempted_ms=fake_now;
 monitor_image.cells.usable_mask=monitor_image.cells.updated_mask=0x7fff;
 for(unsigned i=0;i<15;i++){
  monitor_image.cells.raw_valid[i]=true;monitor_image.cells.raw_mv[i]=4000;
  monitor_image.cells.last_update_ms[i]=fake_now;
 }
 monitor_image.temperature.image.usable_mask=monitor_image.temperature.image.valid_mask=0xffffff;
 monitor_image.temperature.image.startup_scan_complete=true;
 for(unsigned i=0;i<24;i++){
  monitor_image.temperature.image.deci_c[i]=250;
  monitor_image.temperature.image.last_update_ms[i]=fake_now;
 }
}
static void release(void)
{
 fake_thread=2;ams_z022_begin_release();ams_z022_voltage_boundary();
 ams_z023_monitor_result(&monitor_image);ams_z022_publish_release();
}
int main(void)
{
 ams_z023_safety_cycle();assert(fail_low_calls==1);
 assert(ams_z022_init());assert(ams_z023_bind(1,2,3));
 fake_thread=4;ams_z022_current_step();assert(!adc_calls);
 for(unsigned p=0;p<8;p++){
  fake_thread=1;for(unsigned i=0;i<5;i++){fake_now+=20;ams_z022_current_step();}
  healthy();fake_thread=2;ams_z023_temperature_result(1,p,(1U<<p)|(1U<<(p+8))|(1U<<(p+16)),0,0);
  release();ams_supervision_t d=safety();
  assert(d.voltage.valid && d.temperature.valid && d.inhibit);
  assert((d.accepted_mask&4)==(p==7?4:0));
 }
 ams_supervision_t d=safety();assert(!d.accepted_mask && d.temp_converted==0xffffff);
 fake_thread=1;fake_mutex_failure=true;fake_now+=20;ams_z022_current_step();fake_mutex_failure=false;
 d=safety();assert((d.accepted_mask&1) && (d.actor[0].faults&AMS_SUP_PUBLICATION));
 fake_thread=1;adc_failure=true;fake_now+=20;ams_z022_current_step();adc_failure=false;
 d=safety();assert((d.accepted_mask&1) && (d.actor[0].faults&AMS_SUP_INVALID));
 ams_measurement_snapshot_t snap;assert(ams_z022_copy_latest(&snap));unsigned sequence=snap.sequence;
 pipeline.store.reader_count[0]=pipeline.store.reader_count[1]=1;
 fake_now+=100;healthy();release();d=safety();
 assert((d.accepted_mask&2) && (d.actor[1].faults&AMS_SUP_PUBLICATION));
 assert(ams_z022_copy_latest(&snap)&&snap.sequence==sequence);
 pipeline.store.reader_count[0]=pipeline.store.reader_count[1]=0;
 healthy();release();d=safety();assert(!(d.actor[1].faults&AMS_SUP_PUBLICATION));
 uint64_t accepted=d.actor[1].sequence;
 /* A cached publication with no completed monitor work earns no progress. */
 fake_thread=2;ams_z022_begin_release();ams_z022_publish_release();d=safety();assert(d.actor[1].sequence==accepted);
 fake_now+=100;healthy();monitor_image.cells.raw_mv[0]=4300;release();
 fake_now+=3001;d=safety();
 assert(d.actor[1].sequence==accepted && !d.voltage.latched && !d.voltage.valid);
 assert((d.sticky_faults&AMS_SUP_PROCESS) && d.stale_mask==7);
 d=safety();assert(d.active_faults&AMS_SUP_DATA_STALE);
 /* Recovery interruption must destroy partial scan coverage. */
 healthy();fake_thread=2;ams_z023_temperature_result(2,0,0x10101,0,0);
 monitor_image.recovery.pending=true;release();d=safety();assert(d.voltage.read_fault);
 healthy();fake_thread=2;for(unsigned p=1;p<8;p++)ams_z023_temperature_result(2,p,0x10101U<<p,0,0);
 release();d=safety();assert(!(d.accepted_mask&4));
 fake_thread=2;ams_z023_temperature_result(2,0,0x10101,0,0);release();d=safety();assert(d.accepted_mask&4);
 /* Rejected/ISR handoff remains fail-low without consuming a record. */
 fake_thread=1;fake_now+=20;ams_z022_current_step();fake_thread=4;ams_z023_safety_cycle();
 fake_isr=true;ams_z023_safety_cycle();fake_isr=false;d=safety();assert(d.accepted_mask&1);
 puts("PASS Z023 production safety cycle + current/pipeline/supervision: healthy/invalid/lock/pins/replay/stall/recovery/owner");
}
