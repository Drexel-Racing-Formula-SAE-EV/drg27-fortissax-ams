#include <ams_core/ams_can_transport.h>
#include <ams_core/ams_can_codec.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static ams_can_transport_t t;
static unsigned calls;
static uint64_t cookie;
static uint32_t tick;
static ams_can_tx_frame_t sent;
static ams_can_send_result_t result;
static bool immediate, fault_in_send;
static ams_can_send_result_t send(void *ctx,const ams_can_tx_frame_t *f,uint64_t id)
{
 assert(ctx==&t); ++calls; cookie=id; sent=*f;
 if(immediate) {
  ams_can_event_t e={.cookie=id,.tick=tick,.success=true};
  assert(ams_can_transport_capture(&t,&e));
 }
 if(fault_in_send) ams_can_transport_fault(&t,AMS_CAN_BUS_OFF);
 return result;
}
static ams_can_tx_frame_t frame(uint32_t now)
{
 ams_can_current_provenance_t p={.latest_sample_ms=now,.sequence=0x12345,
  .valid=true,.calibration_confident=true,.calibration_id=7,.uncertainty_ma=2};
 ams_can_tx_frame_t f; assert(ams_can_encode_current_diagnostic(&p,now,&f));return f;
}
static bool recover(void)
{
 if(!ams_can_transport_recovery_begin(&t)) return false;
 return ams_can_transport_recover(&t,true,true);
}
static void setup(bool tx)
{
 calls=0; cookie=0; tick=0; result=AMS_CAN_SEND_ACCEPTED;
 immediate=false; fault_in_send=false;
 ams_can_transport_init(&t,tx);
 assert(!ams_can_transport_service(&t,0,send,&t));
 assert(ams_can_transport_recovery_begin(&t));
 assert(!ams_can_transport_recover(&t,false,true));
 assert(!ams_can_transport_recover(&t,true,false));
 assert(recover());
}
static void publish(uint32_t now)
{ ams_can_tx_frame_t f=frame(now); assert(ams_can_transport_publish(&t,now,now,&f)); }
static void complete(uint64_t id,uint32_t now,bool ok)
{ ams_can_event_t e={.cookie=id,.tick=now,.success=ok};assert(ams_can_transport_capture(&t,&e)); }
static void callback_tests(void)
{
 setup(true);publish(0);immediate=true;
 assert(ams_can_transport_service(&t,0,send,&t));
 assert(t.submitted==1 && t.completed==0); /* enqueue is not delivery */
 uint64_t old=cookie;
 assert(ams_can_transport_service(&t,1,send,&t));assert(t.completed==1);
 complete(old,2,true);assert(ams_can_transport_service(&t,2,send,&t));
 assert(t.completed==1 && t.rejected_callbacks==1);
 assert(t.scheduler.protected_required_complete_count==0);
 publish(3);tick=3;immediate=false;assert(ams_can_transport_service(&t,3,send,&t));
 uint64_t newer=cookie;assert(newer>old);
 complete(old,4,true);assert(ams_can_transport_service(&t,4,send,&t));assert(t.completed==1);
 complete(newer,5,false);assert(ams_can_transport_service(&t,5,send,&t));
 assert(t.inhibited && (t.history&AMS_CAN_COMPLETION_ERROR));
 uint64_t progress=t.service_sequence;
 assert(!ams_can_transport_service(&t,6,send,&t));assert(t.service_sequence==progress);
 assert(recover());
 assert(!t.scheduler.detail_active.valid);publish(7);
 assert(ams_can_transport_service(&t,7,send,&t));assert(cookie>newer);
 complete(newer,8,true);assert(ams_can_transport_service(&t,8,send,&t));assert(t.completed==1);
}
static void send_failures(void)
{
 setup(true);publish(0);result=AMS_CAN_SEND_BUSY;
 assert(ams_can_transport_service(&t,0,send,&t));assert(calls==1 && !t.submitted && !t.inhibited);
 uint64_t old=cookie;
 result=AMS_CAN_SEND_ACCEPTED;assert(ams_can_transport_service(&t,1,send,&t));
 assert(calls==2 && cookie>old && t.submitted==1);
 complete(old,2,true);assert(ams_can_transport_service(&t,2,send,&t));assert(!t.completed);
 setup(true);publish(0);result=AMS_CAN_SEND_FAILED;immediate=true;
 assert(ams_can_transport_service(&t,0,send,&t));assert(t.inhibited && !t.completed && !t.submitted);
 assert(recover());assert(!t.count && !t.completed);
 setup(true);publish(0);fault_in_send=true;
 assert(ams_can_transport_service(&t,0,send,&t));assert(t.inhibited && calls==1 && !t.completed);
 assert(t.history&AMS_CAN_BUS_OFF);
 setup(true);publish(0);assert(ams_can_transport_service(&t,0,send,&t));
 assert(ams_can_transport_service(&t,100,send,&t));assert(!t.inhibited);
 assert(ams_can_transport_service(&t,101,send,&t));assert(t.inhibited && (t.history&AMS_CAN_TX_TIMEOUT));
}
static void bounds(void)
{
 setup(false);ams_can_tx_frame_t f=frame(0);
 assert(!ams_can_transport_publish(&t,0,0,&f));
 ams_can_event_t e={.rx=true,.frame=f,.tick=0};
 for(unsigned i=0;i<AMS_CAN_EVENT_CAPACITY;i++)assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,0,send,&t));
 assert(t.rx_accepted==AMS_CAN_EVENT_BUDGET && t.count==AMS_CAN_EVENT_CAPACITY-AMS_CAN_EVENT_BUDGET && !calls);
 e.remote=true;assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,0,send,&t));
 assert(ams_can_transport_service(&t,0,send,&t));assert(t.rx_rejected==1);
 e.remote=false;e.fd=true;assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,0,send,&t));assert(t.rx_rejected==2);
 e.fd=false;e.tick=1;assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,102,send,&t));assert(t.rx_rejected==3);
 e.tick=0;e.frame.data[7]=50;assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,51,send,&t));assert(t.rx_rejected==4);
 e.tick=51;e.frame.data[7]=1;assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,52,send,&t));assert(t.last_rx.data[7]==2);
 setup(true);publish(0);assert(ams_can_transport_service(&t,0,send,&t));
 e=(ams_can_event_t){.cookie=cookie,.success=true};
 for(unsigned i=0;i<AMS_CAN_EVENT_CAPACITY;i++) assert(ams_can_transport_capture(&t,&e));
 assert(!ams_can_transport_capture(&t,&e));
 assert(!ams_can_transport_service(&t,0,send,&t));assert(t.inhibited && !t.completed);
 assert(t.history&AMS_CAN_LOSS);
 assert(recover());assert(t.count==0 && !t.scheduler.detail_active.valid);
 atomic_store(&t.event_lock,true);assert(!ams_can_transport_capture(&t,&e));
 atomic_store(&t.event_lock,false);assert(!ams_can_transport_service(&t,0,send,&t));
 assert(t.inhibited);
}
static void times_and_exhaustion(void)
{
 setup(true);publish(0);assert(ams_can_transport_service(&t,0,send,&t));
 complete(cookie,100,true);assert(ams_can_transport_service(&t,150,send,&t));
 assert(t.completed==1 && !t.inhibited);
 setup(true);publish(0);assert(ams_can_transport_service(&t,0,send,&t));
 complete(cookie,101,true);assert(ams_can_transport_service(&t,101,send,&t));
 assert(t.completed==0 && t.inhibited && (t.history&AMS_CAN_TX_TIMEOUT));
 assert(recover());
 uint64_t late_cookie=cookie;
 publish(102);assert(ams_can_transport_service(&t,102,send,&t));
 complete(late_cookie,103,true);assert(ams_can_transport_service(&t,103,send,&t));
 assert(t.completed==0 && t.rejected_callbacks==1);
 setup(true);publish(0);assert(ams_can_transport_service(&t,101,send,&t));assert(!calls);
 setup(true);publish(0);assert(ams_can_transport_service(&t,80,send,&t));assert(sent.data[7]==80);
 complete(cookie,79,true);assert(ams_can_transport_service(&t,81,send,&t));
 assert(t.inhibited && !t.completed && (t.history&AMS_CAN_TIME_ERROR));
 setup(true);publish(UINT32_MAX-10);tick=5;immediate=true;
 assert(ams_can_transport_service(&t,5,send,&t));assert(sent.data[7]==16);
 assert(ams_can_transport_service(&t,6,send,&t));assert(t.completed==1);
 setup(true);t.next_cookie=UINT64_MAX;publish(0);
 assert(ams_can_transport_service(&t,0,send,&t));assert(t.terminal && !calls);
 assert(!recover());
 setup(true);t.published_generation=UINT32_MAX;ams_can_tx_frame_t f=frame(0);
 assert(!ams_can_transport_publish(&t,0,0,&f));assert(t.terminal);
 setup(false);t.controller_epoch=UINT32_MAX;
 assert(!recover());assert(t.terminal);
 setup(false);t.service_sequence=UINT64_MAX;
 assert(!ams_can_transport_service(&t,0,send,&t));assert(t.terminal);
}
static void recovery_and_rx(void)
{
 setup(true);
 ams_can_tx_frame_t delayed=frame(0);
 assert(!ams_can_transport_publish(&t,101,0,&delayed));
 assert(!ams_can_transport_publish(&t,0,1,&delayed));
 assert(ams_can_transport_publish(&t,80,0,&delayed));
 assert(ams_can_transport_service(&t,90,send,&t));assert(sent.data[7]==90);
 setup(true);
 assert(!ams_can_transport_recover(&t,true,true)); /* no begin/proof epoch */
 ams_can_transport_fault(&t,AMS_CAN_BUS_OFF);
 assert(ams_can_transport_recovery_begin(&t));
 /* Same bit arriving after readiness proof must NOT be acknowledged as old. */
 ams_can_transport_fault(&t,AMS_CAN_BUS_OFF);
 assert(!ams_can_transport_recover(&t,true,true));
 assert(t.inhibited && (t.history&AMS_CAN_BUS_OFF));
 assert(recover());
 ams_can_event_t e={.rx=true,.frame=frame(0),.tick=0};
 assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,1,send,&t));
 ams_can_tx_frame_t out;
 assert(ams_can_transport_copy_rx(&t,100,&out) && out.data[7]==100);
 assert(!ams_can_transport_copy_rx(&t,101,&out) && out.data[2]==0);
 assert(!ams_can_transport_copy_rx(&t,0,&out)); /* before observation */
 assert(recover());assert(!ams_can_transport_copy_rx(&t,2,&out));
 e.tick=2;e.frame=frame(2);assert(ams_can_transport_capture(&t,&e));
 assert(ams_can_transport_service(&t,2,send,&t));
 ams_can_transport_fault(&t,AMS_CAN_BUS_OFF);
 assert(!ams_can_transport_copy_rx(&t,2,&out) && out.data[2]==0);
}
static void codec(void)
{
 ams_can_tx_frame_t f;
 ams_can_current_provenance_t p={.latest_sample_ms=20,.sequence=0x12345,.valid=true,
 .calibration_confident=true,.calibration_id=3,.uncertainty_ma=1};
 const uint8_t golden[8]={1,2,1,1,0x23,0x45,0,100};
 assert(ams_can_encode_current_diagnostic(&p,120,&f));assert(!memcmp(f.data,golden,8));
 assert(ams_can_bench_frame_allowed(&f));
 assert(ams_can_encode_current_diagnostic(&p,121,&f));
 const uint8_t invalid[8]={0,0,0,1,0,0,0,0};assert(!memcmp(f.data,invalid,8));
 p.uncertainty_ma=UINT16_MAX;assert(ams_can_encode_current_diagnostic(&p,20,&f));assert(f.data[1]==1);
 p.uncertainty_ma=0;assert(ams_can_encode_current_diagnostic(&p,20,&f));assert(f.data[1]==1);
 p.uncertainty_ma=1;p.calibration_id=0;assert(ams_can_encode_current_diagnostic(&p,20,&f));assert(f.data[1]==1);
 assert(ams_can_encode_current_diagnostic(&p,19,&f));assert(!memcmp(f.data,invalid,8));
 f=frame(0);f.id=0x680;assert(!ams_can_bench_frame_allowed(&f));
 f=frame(0);f.ide=1;assert(!ams_can_bench_frame_allowed(&f));
 f=frame(0);f.dlc=7;assert(!ams_can_bench_frame_allowed(&f));
 f=frame(0);f.data[0]=2;assert(!ams_can_bench_frame_allowed(&f));
 f=frame(0);f.request_id=1;assert(!ams_can_bench_frame_allowed(&f));
 f=frame(0);f.data[7]=101;assert(!ams_can_bench_frame_allowed(&f));
}
int main(void)
{
 recovery_and_rx();codec();callback_tests();send_failures();bounds();times_and_exhaustion();
 printf("PASS Z024 codec/transport: bounded callbacks, identity, overflow, recovery, expiry; storage=%zu\n",sizeof(t));
}
