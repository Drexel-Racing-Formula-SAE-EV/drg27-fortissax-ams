#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#define main z022_main
#include "../z022/adapter_test.c"
#undef main
int main(void)
{
 assert(ams_z023_bind(1,2,4));
 (void)z022_main();
 fake_thread=4;ams_supervision_t d;
 assert(ams_z023_poll(&d) && d.accepted_mask==1);
 assert(d.actor[0].faults&AMS_SUP_INVALID);
 assert(ams_z023_poll(&d) && !d.accepted_mask);
 puts("PASS Z023 actual current-worker hooks");
}
