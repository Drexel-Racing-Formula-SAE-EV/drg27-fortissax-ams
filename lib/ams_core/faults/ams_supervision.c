#include <ams_core/ams_supervision.h>
#include <stddef.h>
#include <string.h>
static const uint32_t timeout[AMS_SUP_ACTORS]={200U,3000U,3000U};
void ams_supervision_init(ams_supervision_t *s,uint32_t now)
{
 if(!s)return;
 memset(s,0,sizeof(*s));s->start_ms=now;s->inhibit=true;
}
bool ams_supervision_next(ams_supervision_record_t *r,uint32_t now,uint32_t faults)
{
 if(!r || r->sequence==UINT64_MAX)return false;
 r->sequence++;r->completed_ms=now;r->faults=faults;r->history|=faults;return true;
}
bool ams_supervision_commit(ams_supervision_t *s,unsigned actor,
 const ams_supervision_record_t *r,uint32_t now)
{
 if(!s || !r || actor>=AMS_SUP_ACTORS)return false;
 if(!r->sequence || r->sequence<=s->actor[actor].sequence ||
    (uint32_t)(now-r->completed_ms)>timeout[actor])return false;
 s->actor[actor]=*r;return true;
}
void ams_supervision_evaluate(ams_supervision_t *s,uint32_t now)
{
 if(!s)return;
 s->accepted_mask=0;s->stale_mask=0;s->active_faults=0;s->evaluated_ms=now;
 for(unsigned a=0;a<AMS_SUP_ACTORS;a++) {
  const ams_supervision_record_t *r=&s->actor[a];
  bool stale=r->sequence ? (uint32_t)(now-r->completed_ms)>timeout[a] :
                          (uint32_t)(now-s->start_ms)>=3000U;
  s->active_faults|=r->faults;
  s->sticky_faults|=r->history;
  if(stale){s->stale_mask|=(uint8_t)(1U<<a);s->active_faults|=AMS_SUP_PROGRESS_STALE;}
  if(!stale && r->sequence>s->consumed[a]) {
   /* Fault/history disposition is committed before accepting progress. */
   s->consumed[a]=r->sequence;s->heartbeat_ms[a]=r->completed_ms;
   s->accepted_mask|=(uint8_t)(1U<<a);
  }
 }
 if(s->actor[AMS_SUP_ADBMS].sequence && ((uint32_t)(now-s->voltage_data_ms)>2500U || (s->stale_mask&(1U<<AMS_SUP_ADBMS)))) {
  s->voltage.valid=false;s->active_faults|=AMS_SUP_DATA_STALE;
 }
 if(s->actor[AMS_SUP_ADBMS].sequence && ((uint32_t)(now-s->temperature_data_ms)>12000U ||
    (s->stale_mask&((1U<<AMS_SUP_ADBMS)|(1U<<AMS_SUP_TEMP))))) {
  s->temperature.valid=false;s->active_faults|=AMS_SUP_DATA_STALE;
 }
 s->sticky_faults|=s->active_faults;
 /* Shadow stage: full-pack and physical qualification remain absent. */
 s->inhibit=true;
}
bool ams_supervision_scan(ams_supervision_scan_t *s,uint32_t generation,
 uint8_t position,uint32_t converted,uint32_t failed,uint32_t suppressed)
{
 if(!s || position>=8U)return false;
 uint32_t mask=(1UL<<position)|(1UL<<(position+8U))|(1UL<<(position+16U));
 if((converted|failed|suppressed)!=mask || (converted&failed) ||
    (converted&suppressed) || (failed&suppressed))return false;
 if(s->generation_valid && generation!=s->generation &&
    (uint32_t)(generation-s->generation)>=0x80000000U)return false;
 if(!s->generation_valid || s->generation!=generation) {
  memset(s,0,sizeof(*s));s->generation_valid=true;s->generation=generation;
 }
 if(s->positions&(1U<<position))return false;
 s->positions|=(uint8_t)(1U<<position);s->converted|=converted;
 s->failed|=failed;s->suppressed|=suppressed;
 return s->positions==0xffU;
}
