#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
_Thread_local uintptr_t fake_thread;
_Thread_local bool fake_isr;
static atomic_uint done;
static void *current(void *arg)
{
 (void)arg;fake_thread=1;
 for(unsigned i=0;i<50000;i++)ams_z023_current_complete(i%2?AMS_SUP_INVALID:AMS_SUP_UNQUALIFIED);
 atomic_fetch_add(&done,1);return NULL;
}
static void *monitor(void *arg)
{
 (void)arg;fake_thread=2;ams_adbms_monitor_platform_snapshot_t m={0};
 for(unsigned i=0;i<10000;i++){
  ams_z023_monitor_result(&m);ams_z023_publication_complete(i%2!=0);
 }
 atomic_fetch_add(&done,1);return NULL;
}
int main(void)
{
 assert(ams_z023_bind(1,2,3));pthread_t c,m;
 assert(!pthread_create(&c,NULL,current,NULL));assert(!pthread_create(&m,NULL,monitor,NULL));
 fake_thread=3;ams_supervision_t d;uint64_t prev[2]={0};
 do {
  assert(ams_z023_poll(&d));
  for(unsigned i=0;i<2;i++){assert(d.actor[i].sequence>=prev[i]);prev[i]=d.actor[i].sequence;}
 }while(atomic_load(&done)!=2);
 assert(!pthread_join(c,NULL)&&!pthread_join(m,NULL));assert(ams_z023_poll(&d));
 assert(d.actor[0].sequence==50000 && d.actor[1].sequence==10000);
 assert((d.sticky_faults&(AMS_SUP_INVALID|AMS_SUP_UNQUALIFIED|AMS_SUP_PUBLICATION))==
        (AMS_SUP_INVALID|AMS_SUP_UNQUALIFIED|AMS_SUP_PUBLICATION));
 puts("PASS Z023 concurrent production adapter: 50000 current and 10000 monitor handoffs");
}
