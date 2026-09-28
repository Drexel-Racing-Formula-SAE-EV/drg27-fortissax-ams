#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#define ams_adbms_monitor_recovery_step scripted_recovery_step
#define main z022_main
#include "../adbms_monitor/adapter_test.c"
#undef main
#undef ams_adbms_monitor_recovery_step
static bool terminal_fixture;
ams_adbms_result_t ams_adbms_monitor_recovery_step(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint32_t now)
{
 if(terminal_fixture){m->recovery.terminal=true;return AMS_ADBMS_RESULT_TRANSPORT_IO;}
 return scripted_recovery_step(m,io,now);
}
int main(void)
{
 assert(ams_z023_bind((k_tid_t)2,(k_tid_t)1,(k_tid_t)3));
 (void)z022_main();
 fake_isr=false;fake_current_thread=3;ams_supervision_t d;
 assert(ams_z023_poll(&d) && !d.accepted_mask);
 fake_current_thread=1;ams_z023_publication_complete(false);
 fake_current_thread=3;assert(ams_z023_poll(&d) && d.accepted_mask==2);
 assert(d.actor[1].faults&AMS_SUP_PUBLICATION);
 assert(ams_z023_poll(&d) && !d.accepted_mask);
 uint64_t accepted=d.actor[1].sequence;
 terminal_fixture=true;fake_current_thread=1;
 ams_adbms_monitor_platform_step(100500);ams_z023_publication_complete(true);
 fake_current_thread=3;assert(ams_z023_poll(&d) && d.actor[1].sequence==accepted+1 && (d.actor[1].faults&AMS_SUP_TERMINAL));
 fake_current_thread=1;ams_adbms_monitor_platform_step(100600);ams_z023_publication_complete(true);
 fake_current_thread=3;assert(ams_z023_poll(&d) && !d.accepted_mask && d.actor[1].sequence==accepted+1);
 puts("PASS Z023 actual monitor hook waits for publication disposition");
}
