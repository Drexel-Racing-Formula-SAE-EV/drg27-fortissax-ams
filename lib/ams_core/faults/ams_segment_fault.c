#include <ams_core/ams_segment_fault.h>
#include <stddef.h>
static void clear(ams_segment_fault_t *s)
{
 bool latch=s->latched;ams_segment_fault_reason_t reason=s->latched_reason;
 *s=(ams_segment_fault_t){0};s->latched=latch;s->latched_reason=reason;
}
static void latch(ams_segment_fault_t *s,ams_segment_fault_reason_t reason)
{s->reason=reason;s->confirmed=true;s->latched=true;s->latched_reason=reason;}
void ams_segment_voltage_fault(ams_segment_fault_t *s,const ams_cell_image_t *c,
 bool coherent,bool statd_valid,uint16_t ov,uint16_t uv,uint32_t now)
{
 if(!s)return;
 bool had_valid=s->valid;uint8_t streak=s->read_streak;clear(s);
 bool full=c && coherent && c->usable_mask==0x7fffU && c->updated_mask==0x7fffU;
 uint16_t min=UINT16_MAX,max=0;
 for(unsigned i=0;c && i<15U;i++) {
  if(!c->raw_valid[i] || (uint32_t)(now-c->last_update_ms[i])>2500U ||
     c->raw_mv[i]<500U || c->raw_mv[i]>5000U)full=false;
  if(c->raw_mv[i]<min)min=c->raw_mv[i];
  if(c->raw_mv[i]>max)max=c->raw_mv[i];
 }
 if(!full){s->read_fault=true;s->reason=AMS_SEG_READ;
  s->read_streak=streak==UINT8_MAX?streak:(uint8_t)(streak+1U);
  s->read_pending=had_valid && s->read_streak<3U;s->valid=s->read_pending;
  s->warning=s->charge_stop=s->read_pending;s->confirmed=!s->read_pending;return;}
 s->valid=true;ov&=0x7fffU;uv&=0x7fffU;
 for(unsigned i=0;i<15U;i++) {
  uint16_t bit=(uint16_t)(1U<<i),v=c->raw_mv[i];
  if(v>=4200U)s->software_ov|=bit;
  if(v<=3000U)s->software_uv|=bit;
  if(statd_valid && ((v>=4220U && !(ov&bit)) || (v<=4180U && (ov&bit)) ||
     (v<=2980U && !(uv&bit)) || (v>=3020U && (uv&bit))))s->hw_disagreement|=bit;
 }
 if(max>=4250U){latch(s,AMS_SEG_OV_SEVERE);return;}
 if(min<=2300U){latch(s,AMS_SEG_UV_SEVERE);return;}
 if(max>=4200U){latch(s,AMS_SEG_OV_HARD);return;}
 if(min<=2500U){latch(s,AMS_SEG_UV_HARD);return;}
 if(max>=4180U){s->reason=AMS_SEG_CHARGE_STOP;s->charge_stop=true;}
 else if(min<=2800U)s->reason=AMS_SEG_UV_SOFT;
 else if(max>=4150U)s->reason=AMS_SEG_OV_WARN;
 else if(min<=3000U)s->reason=AMS_SEG_UV_WARN;
 else if(s->hw_disagreement)s->reason=AMS_SEG_HW_DISAGREE;
 else if(statd_valid && (ov|uv))s->reason=AMS_SEG_HW_WARN;
 s->warning=s->reason!=AMS_SEG_NONE;
}
void ams_segment_temperature_fault(ams_segment_fault_t *s,const ams_temp_image_t *t,
 bool coherent,uint32_t now)
{
 if(!s)return;
 uint32_t pending=s->pending_ms;ams_segment_fault_reason_t reason=s->pending_reason;clear(s);
 bool full=t && coherent && t->startup_scan_complete && t->usable_mask==0xffffffU;
 int16_t min=INT16_MAX,max=INT16_MIN;
 for(unsigned i=0;t && i<24U;i++) {
  if((uint32_t)(now-t->last_update_ms[i])>12000U)full=false;
  if(t->deci_c[i]<min)min=t->deci_c[i];
  if(t->deci_c[i]>max)max=t->deci_c[i];
 }
 if(!full){s->read_fault=s->confirmed=s->fan_max=true;s->reason=AMS_SEG_NOT_READY;return;}
 s->valid=true;
 if(max>=600){
  s->reason=max>=650?AMS_SEG_HOT_SEVERE:AMS_SEG_HOT_HARD;
  if(reason!=s->reason)pending=0;
  s->pending_reason=s->reason;s->pending_ms=pending>UINT32_MAX-100U?UINT32_MAX:pending+100U;
  s->pending=s->warning=s->fan_max=true;
  if(s->pending_ms>=2000U)latch(s,s->reason);
  return;
 }
 if(max>=500){s->fan_max=s->charge_stop=s->warning=true;s->reason=AMS_SEG_HOT_FAN;}
 else if(max>=450){s->charge_stop=s->warning=true;s->reason=AMS_SEG_HOT_CHARGE;}
 else if(min<=0){s->charge_stop=s->warning=true;s->reason=AMS_SEG_COLD_CHARGE;}
}
