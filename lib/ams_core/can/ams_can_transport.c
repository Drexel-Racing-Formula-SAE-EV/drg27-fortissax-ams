#include <ams_core/ams_can_transport.h>
#include <limits.h>
#include <string.h>
_Static_assert(ATOMIC_BOOL_LOCK_FREE==2 && ATOMIC_INT_LOCK_FREE==2,
 "CAN callback capture requires always-lock-free atomics");
static void inc(uint32_t *v) { if (*v != UINT32_MAX) ++*v; }
static bool lock(ams_can_transport_t *t)
{ return !atomic_exchange_explicit(&t->event_lock,true,memory_order_acquire); }
static void unlock(ams_can_transport_t *t)
{ atomic_store_explicit(&t->event_lock,false,memory_order_release); }
static void disposition(ams_can_transport_t *t)
{
 t->faults |= atomic_exchange_explicit(&t->async_faults,0,memory_order_acq_rel);
 t->history |= t->faults;
 if(t->faults) t->inhibited=true;
}
void ams_can_transport_init(ams_can_transport_t *t,bool tx)
{
 if(!t) return;
 memset(t,0,sizeof(*t));
 atomic_init(&t->event_lock,false); atomic_init(&t->async_faults,0);
 ams_can_tx_scheduler_init(&t->scheduler);
 t->controller_epoch=1; t->tx_enabled=tx;
 /* Init is a software state only; readiness must be explicitly established. */
 t->inhibited=true;
}
void ams_can_transport_fault(ams_can_transport_t *t,unsigned faults)
{ if(t) atomic_fetch_or_explicit(&t->async_faults,faults,memory_order_release); }
bool ams_can_transport_capture(ams_can_transport_t *t,const ams_can_event_t *e)
{
 if(!t || !e) return false;
 if(!lock(t)) { ams_can_transport_fault(t,AMS_CAN_LOSS); return false; }
 bool ok=t->count<AMS_CAN_EVENT_CAPACITY;
 if(ok) { t->events[(t->head+t->count)%AMS_CAN_EVENT_CAPACITY]=*e; ++t->count; }
 unlock(t);
 if(!ok) ams_can_transport_fault(t,AMS_CAN_LOSS);
 return ok;
}
bool ams_can_bench_frame_allowed(const ams_can_tx_frame_t *f)
{
 /* Only the existing read-only current provenance frame is enabled in this
  * portable bench seam. Authority/status/power/charger/tuning frames withheld. */
 if(!f || f->id!=0x68bU || f->ide || f->dlc!=8 ||
    f->tx_class!=AMS_CAN_TX_CLASS_PROTECTED_ADVISORY || f->request_id ||
    f->data[3]!=1) return false;
 if(f->data[0]==0) return f->data[1]==0 && f->data[2]==0 &&
     f->data[4]==0 && f->data[5]==0 && f->data[6]==0 && f->data[7]==0;
 return f->data[0]==1 && (f->data[1]==1 || f->data[1]==2) &&
        f->data[2]==1 && f->data[6]==0 && f->data[7]<=100;
}
bool ams_can_transport_publish(ams_can_transport_t *t,uint32_t now,uint32_t encoded_ms,
 const ams_can_tx_frame_t *f)
{
 if(!t) return false;
 disposition(t);
 if(t->inhibited || !t->tx_enabled || !ams_can_bench_frame_allowed(f)) return false;
 uint32_t queued_age=now-encoded_ms;
 if(queued_age>100U || (f->data[2] && f->data[7]>100U-queued_age)) return false;
 ams_can_tx_frame_t frozen=*f;
 if(frozen.data[2]) frozen.data[7]=(uint8_t)(frozen.data[7]+queued_age);
 if(t->published_generation==UINT32_MAX) {
  t->terminal=true; t->faults|=AMS_CAN_EXHAUSTED; disposition(t); return false;
 }
 /* Advisory-only: this must never claim completion of the required 680-687 set.
  * Detail storage retains active+pending behavior without required-set credit. */
 return ams_can_tx_publish_detail(&t->scheduler,++t->published_generation,now,&frozen,1);
}
static void event(ams_can_transport_t *t,const ams_can_event_t *e,uint32_t now)
{
 if(e->rx) {
  if(e->remote || e->fd || (uint32_t)(now-e->tick)>100 || !ams_can_bench_frame_allowed(&e->frame)) {
   inc(&t->rx_rejected); return;
  }
  uint32_t elapsed=now-e->tick;
  if(e->frame.data[2] && e->frame.data[7]>100-elapsed) {
   inc(&t->rx_rejected); return;
  }
  t->last_rx=e->frame;
  if(t->last_rx.data[2]) t->last_rx.data[7]=(uint8_t)(t->last_rx.data[7]+elapsed);
  t->last_rx_ms=now; t->rx_seen=true; inc(&t->rx_accepted); return;
 }
 for(unsigned i=0;i<AMS_CAN_OUTSTANDING;i++) {
  ams_can_outstanding_t *s=&t->outstanding[i];
  if(!s->used || s->cookie!=e->cookie) continue;
  if((uint32_t)(e->tick-s->submitted_ms)>=0x80000000U ||
     (uint32_t)(now-e->tick)>=0x80000000U) {
   t->faults|=AMS_CAN_TIME_ERROR; return;
  }
  /* Event time decides the completion deadline. Draining a late callback
   * must not clear ownership before the outstanding-request timeout sees it.
   * Conversely, a timely completion observed by a delayed owner is valid. */
  if((uint32_t)(e->tick-s->submitted_ms)>100U) {
   t->faults|=AMS_CAN_TX_TIMEOUT; return;
  }
  ams_can_tx_mark_complete(&t->scheduler,&s->token,e->success,e->tick);
  s->used=false;
  if(e->success) inc(&t->completed); else t->faults|=AMS_CAN_COMPLETION_ERROR;
  return;
 }
 inc(&t->rejected_callbacks);
}
bool ams_can_transport_service(ams_can_transport_t *t,uint32_t now,
 ams_can_send_fn send,void *context)
{
 if(!t || t->terminal) return false;
 disposition(t);
 /* Cached inhibited state is not newly completed worker progress. */
 if(t->inhibited) return false;
 for(unsigned n=0;n<AMS_CAN_EVENT_BUDGET;n++) {
  ams_can_event_t e; bool have;
  if(!lock(t)) { t->faults|=AMS_CAN_LOSS; break; }
  have=t->count!=0;
  if(have) { e=t->events[t->head]; t->head=(t->head+1)%AMS_CAN_EVENT_CAPACITY; --t->count; }
  unlock(t);
  if(!have) break;
  event(t,&e,now);
  disposition(t);
  if(t->inhibited) break;
 }
 for(unsigned i=0;i<AMS_CAN_OUTSTANDING;i++)
  if(t->outstanding[i].used && (uint32_t)(now-t->outstanding[i].submitted_ms)>100)
   t->faults|=AMS_CAN_TX_TIMEOUT;
 disposition(t);
 for(unsigned n=0;n<AMS_CAN_SEND_BUDGET && t->tx_enabled && !t->inhibited;n++) {
  unsigned i;
  for(i=0;i<AMS_CAN_OUTSTANDING && t->outstanding[i].used;i++) {}
  if(i==AMS_CAN_OUTSTANDING || !send) break;
  ams_can_outstanding_t *s=&t->outstanding[i]; ams_can_tx_frame_t f;
  if(!ams_can_tx_reserve_next(&t->scheduler,&s->token,&f)) break;
  if(!ams_can_bench_frame_allowed(&f)) { t->faults|=AMS_CAN_BAD_FRAME; break; }
  /* Expire current provenance at send time, not just generation creation. */
  uint32_t published=t->scheduler.detail_active.publish_tick;
  uint32_t elapsed=now-published;
  uint32_t age=((uint32_t)f.data[6]<<8)|f.data[7];
  if(elapsed>100 || (f.data[2] && age>100-elapsed)) {
   ams_can_tx_mark_loaded(&t->scheduler,&s->token);
   ams_can_tx_mark_complete(&t->scheduler,&s->token,false,now);
   continue;
  }
  if(f.data[2]) { age+=elapsed; f.data[6]=(uint8_t)(age>>8); f.data[7]=(uint8_t)age; }
  if(t->next_cookie==UINT64_MAX) { t->faults|=AMS_CAN_EXHAUSTED; t->terminal=true; break; }
  s->cookie=++t->next_cookie; s->submitted_ms=now; s->used=true;
  /* Capture only queues events, so synchronous callbacks cannot resolve the
   * scheduler reservation before the ACCEPTED result marks it loaded. */
  disposition(t);
  if(t->inhibited) break;
  ams_can_send_result_t result=send(context,&f,s->cookie);
  if(result==AMS_CAN_SEND_ACCEPTED) {
   ams_can_tx_mark_loaded(&t->scheduler,&s->token); inc(&t->submitted);
  } else {
   s->used=false; ams_can_tx_load_failed(&t->scheduler,&s->token);
   if(result!=AMS_CAN_SEND_BUSY) t->faults|=AMS_CAN_SEND_ERROR;
   break;
  }
  disposition(t);
 }
 disposition(t);
 if(t->service_sequence==UINT64_MAX) {
  t->terminal=true; t->faults|=AMS_CAN_EXHAUSTED; disposition(t); return false;
 }
 ++t->service_sequence; t->last_service_ms=now;
 return true; /* bounded fault disposition can be progress, never delivery */
}
bool ams_can_transport_copy_rx(ams_can_transport_t *t,uint32_t now,
 ams_can_tx_frame_t *out)
{
 if(!t || !out || out==&t->last_rx) return false;
 memset(out,0,sizeof(*out));
 disposition(t);
 if(t->inhibited || !t->rx_seen) return false;
 uint32_t elapsed=now-t->last_rx_ms;
 if(elapsed>100U || (t->last_rx.data[2] && t->last_rx.data[7]>100U-elapsed))
  return false;
 *out=t->last_rx;
 if(out->data[2]) out->data[7]=(uint8_t)(out->data[7]+elapsed);
 return true;
}
bool ams_can_transport_recovery_begin(ams_can_transport_t *t)
{
 if(!t || t->terminal) return false;
 /* Atomic exchange in disposition acknowledges only earlier faults. A new
  * fault after that exchange remains pending until recovery rechecks it. */
 disposition(t);
 t->faults=0; t->inhibited=true; t->recovery_pending=true; t->rx_seen=false;
 return true;
}
bool ams_can_transport_recover(ams_can_transport_t *t,bool ready,bool settled)
{
 if(!t || !t->recovery_pending || !ready || !settled || t->terminal) return false;
 if(!lock(t)) return false;
 if(t->controller_epoch==UINT32_MAX) {
  unlock(t); t->terminal=true; t->faults|=AMS_CAN_EXHAUSTED; disposition(t); return false;
 }
 /* Do not erase faults arriving after recovery_begin, even if their bits
  * equal the fault which caused the previous recovery attempt. */
 disposition(t);
 if(t->faults) { t->recovery_pending=false; unlock(t); return false; }
 t->head=0; t->count=0;
 memset(t->outstanding,0,sizeof(t->outstanding));
 ams_can_tx_scheduler_init(&t->scheduler);
 t->scheduler.controller_epoch=++t->controller_epoch;
 t->recovery_pending=false; t->rx_seen=false;
 memset(&t->last_rx,0,sizeof(t->last_rx));
 t->inhibited=false;
 unlock(t);
 disposition(t);
 return !t->inhibited;
}
