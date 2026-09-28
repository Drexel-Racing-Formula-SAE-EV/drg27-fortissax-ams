#include <ams_core/ams_can_transport.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
static ams_can_transport_t t;
static atomic_uint done;
static void *callback(void *unused)
{
 (void)unused;
 for(unsigned n=0;n<10000;n++) {
  ams_can_event_t e={.cookie=(uint64_t)n+1,.tick=0,.success=true};
  (void)ams_can_transport_capture(&t,&e);
 }
 atomic_fetch_add(&done,1);return NULL;
}
int main(void)
{
 pthread_t a,b;atomic_init(&done,0);ams_can_transport_init(&t,false);
 assert(ams_can_transport_recovery_begin(&t));
 assert(ams_can_transport_recover(&t,true,true));
 assert(!pthread_create(&a,NULL,callback,NULL));assert(!pthread_create(&b,NULL,callback,NULL));
 do {
  (void)ams_can_transport_service(&t,0,NULL,NULL);
  if(t.inhibited) { (void)ams_can_transport_recovery_begin(&t);
   (void)ams_can_transport_recover(&t,true,true); }
 } while(atomic_load(&done)!=2);
 assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));
 assert(t.count<=AMS_CAN_EVENT_CAPACITY && t.completed==0 && t.submitted==0);
 puts("PASS Z024 concurrent callback capture: 20000 events, owner drain/recovery");
}
