#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t fake_now;uintptr_t fake_thread=1;bool fake_isr,fake_locked;
int main(void)
{
 ams_supervision_t d;ams_adbms_monitor_platform_snapshot_t m={0};
 assert(!ams_z023_poll(&d));assert(!ams_z023_bind(1,1,3));assert(ams_z023_bind(1,2,3));
 assert(!ams_z023_bind(1,2,3));fake_thread=4;ams_z023_current_complete(AMS_SUP_INVALID);
 fake_thread=3;assert(ams_z023_poll(&d)&&!d.accepted_mask);
 fake_thread=1;fake_isr=true;ams_z023_current_complete(AMS_SUP_INVALID);fake_isr=false;
 ams_z023_current_complete(AMS_SUP_INVALID);ams_z023_current_complete(0);
 fake_thread=3;assert(ams_z023_poll(&d)&&d.accepted_mask==1 && (d.sticky_faults&AMS_SUP_INVALID));
 assert(ams_z023_poll(&d)&&!d.accepted_mask);
 fake_thread=2;ams_z023_publication_complete(true);
 fake_thread=3;assert(ams_z023_poll(&d)&&!d.accepted_mask);
 fake_thread=2;m.initialized=m.config_verified=m.balance_mute_verified=m.balance_durable_zero_verified=true;
 m.cells.usable_mask=0x7fff;m.temperature.image.usable_mask=0xffffff;
 for(unsigned p=0;p<8;p++){
  ams_z023_temperature_result(1,p,(1U<<p)|(1U<<(p+8)),0,1U<<(p+16));
  ams_z023_monitor_result(&m);
  fake_thread=3;assert(ams_z023_poll(&d)&&!(d.accepted_mask&4));
  fake_thread=2;ams_z023_publication_complete(true);
 }
 fake_thread=3;assert(ams_z023_poll(&d)&&(d.accepted_mask&4)&&(d.actor[2].faults&AMS_SUP_INVALID));
 assert(d.temp_converted==0xffff && d.temp_suppressed==0xff0000 && !d.temp_failed && d.temp_generation==1);
 assert(ams_z023_poll(&d)&&!d.accepted_mask);
 fake_thread=2;ams_z023_temperature_result(2,0,0x10101,0,0);
 m.recovery.pending=true;ams_z023_monitor_result(&m);ams_z023_publication_complete(false);
 fake_thread=3;assert(ams_z023_poll(&d)&&d.actor[1].faults&AMS_SUP_PUBLICATION);
 fake_now=4000;assert(ams_z023_poll(&d)&&d.stale_mask==7 && d.inhibit);
 puts("PASS Z023 production supervisor adapter: explicit owners, ISR, publication handoff, scan and shadow progress");
}
