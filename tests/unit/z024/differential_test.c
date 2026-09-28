#include <ams_core/ams_can_tx_scheduler.h>
#include "oracle_prototypes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng=0x024CAFE;
static uint32_t next(void){rng=rng*1664525U+1013904223U;return rng;}
int main(void)
{
 ams_can_tx_scheduler_t a,b;ams_can_tx_scheduler_init(&a);oracle_ams_can_tx_scheduler_init(&b);
 ams_can_tx_token_t ta[3]={{0}},tb[3]={{0}};
 ams_can_tx_frame_t f[3]={{.id=0x680,.dlc=8,.tx_class=AMS_CAN_TX_CLASS_PROTECTED_REQUIRED},
 {.id=0x681,.dlc=8,.tx_class=AMS_CAN_TX_CLASS_PROTECTED_REQUIRED},
 {.id=0x68b,.dlc=8,.tx_class=AMS_CAN_TX_CLASS_PROTECTED_ADVISORY}};
 uint32_t generation=UINT32_MAX-100, tick=UINT32_MAX-200;
 for(unsigned n=0;n<100000;n++) {
  uint32_t r=next(), x=0,y=0;unsigned slot=(r>>16)%3;bool ra,rb;tick+=r&7;
  switch((r>>4)%11) {
  case 0:++generation;assert(ams_can_tx_publish_protected(&a,generation,tick,f,3,2,&x)==oracle_ams_can_tx_publish_protected(&b,generation,tick,f,3,2,&y));assert(x==y);break;
  case 1:++generation;assert(ams_can_tx_publish_detail(&a,generation,tick,f,3)==oracle_ams_can_tx_publish_detail(&b,generation,tick,f,3));break;
  case 2:++generation;assert(ams_can_tx_publish_critical(&a,generation,tick,f,1)==oracle_ams_can_tx_publish_critical(&b,generation,tick,f,1));break;
  case 3:{ams_can_tx_frame_t fa={0},fb={0};ra=ams_can_tx_reserve_next(&a,&ta[slot],&fa);rb=oracle_ams_can_tx_reserve_next(&b,&tb[slot],&fb);assert(ra==rb);if(ra)assert(!memcmp(&fa,&fb,sizeof(fa)));break;}
  case 4:ams_can_tx_mark_loaded(&a,&ta[slot]);oracle_ams_can_tx_mark_loaded(&b,&tb[slot]);break;
  case 5:ams_can_tx_load_failed(&a,&ta[slot]);oracle_ams_can_tx_load_failed(&b,&tb[slot]);break;
  case 6:ams_can_tx_mark_abort_requested(&a,&ta[slot]);oracle_ams_can_tx_mark_abort_requested(&b,&tb[slot]);break;
  case 7:ams_can_tx_mark_abort_failed(&a,&ta[slot]);oracle_ams_can_tx_mark_abort_failed(&b,&tb[slot]);break;
  case 8:ams_can_tx_mark_complete(&a,&ta[slot],(r&1)!=0,tick);oracle_ams_can_tx_mark_complete(&b,&tb[slot],(r&1)!=0,tick);break;
  case 9:if((r&255)==0){ams_can_tx_controller_epoch_reset(&a);oracle_ams_can_tx_controller_epoch_reset(&b);}break;
  default:++generation;assert(ams_can_tx_publish_tuning(&a,generation,tick,f,3)==oracle_ams_can_tx_publish_tuning(&b,generation,tick,f,3));break;
  }
  assert(!memcmp(&a,&b,sizeof(a)));
 }
 puts("PASS Z024 frozen scheduler differential: 100000 transitions including generation/time wrap");
}
