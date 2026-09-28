#include <ams_core/ams_segment_consumer.h>
#include <ams_core/ams_cell_image.h>
#include <ams_core/ams_temp_image.h>
#include <math.h>
#include <float.h>
#include <string.h>
static void invalid(ams_segment_consumer_t *c,uint32_t reason) {
 c->current_trusted=false;
 for(unsigned i=0;i<AMS_PHYSICAL_SEGMENT_COUNT;i++){c->segment[i].valid=0;c->segment[i].fault_flags=reason;}
}
void ams_segment_consumer_init(ams_segment_consumer_t *c) {
 if(!c)return;
 memset(c,0,sizeof(*c));
 for(unsigned i=0;i<AMS_PHYSICAL_SEGMENT_COUNT;i++) {
  ams_ekf_config_t cfg;ams_ekf_make_segment_config(&cfg,(uint8_t)i);ams_ekf_init(&c->segment[i],&cfg);
 }
}
bool ams_segment_consumer_step(ams_segment_consumer_t *c,const ams_measurement_snapshot_t *s,uint32_t now) {
 if(!c)return false;
 if(!s||!s->sequence||(uint32_t)(now-s->publication_tick)>100U||
    (uint32_t)(now-s->voltage_complete_tick)>100U) {
  invalid(c,AMS_EKF_FAULT_STALE_INPUT);return false;
 }
 if(s->sequence==c->sequence)return false;
 if(c->sequence&&(uint32_t)(s->sequence-c->sequence)>=0x80000000U) {
  invalid(c,AMS_EKF_FAULT_EPOCH_TIMING);return false;
 }
 uint32_t elapsed=c->sequence?s->voltage_complete_tick-c->voltage_tick:100U;
 bool timing=elapsed>=1U&&elapsed<=1000U;
 bool contiguous=s->current.valid&&(!c->total_seen||s->current.total_invalid_sample_count==c->invalid_total);
 double charge=s->current.charge_As;
 if(c->total_seen) {
  double a=s->current.total_charge_As,b=c->charge_total;
  /* Validate before subtracting or narrowing. Malformed cumulative values
   * must not overflow arithmetic before the later isfinite check. */
  if(!isfinite(a)||!isfinite(b)||(b<0.0&&a>DBL_MAX+b)||
     (b>0.0&&a< -DBL_MAX+b)) charge=NAN;
  else charge=a-b;
 }
 float dt=(float)elapsed/1000.0f;
 float current=s->current.average_A;
 if(c->sequence&&timing) {
  current=NAN;
  /* Same physical input limit as the current-window producer. This also
   * proves the double-to-float conversion is representable. */
  if(isfinite(charge)&&fabs(charge)<=1500.0*(double)dt)
   current=(float)(charge/(double)dt);
 }
 bool ready=timing&&contiguous&&isfinite(charge)&&isfinite(current)&&
  fabsf(current)<=1500.0f&&
  s->current.end_tick==s->voltage_complete_tick&&
  (uint32_t)(now-s->current.latest_sample_tick)<=100U&&
  (s->validity_flags&AMS_MEAS_VALID_CURRENT)&&(s->validity_flags&AMS_MEAS_BALANCE_RECOVERED);
 c->current_trusted=s->current.calibration_record_confident&&s->current.calibration_id!=0U&&
  s->current.uncertainty_mA!=0U&&s->current.uncertainty_mA!=UINT16_MAX;
 float uncertainty=c->current_trusted?(float)s->current.uncertainty_mA/1000.0f:0.0f;
 for(unsigned seg=0;seg<AMS_PHYSICAL_SEGMENT_COUNT;seg++) {
  ams_ekf_instance_t *e=&c->segment[seg];float voltage=0,temp=0;unsigned nt=0;
  bool good=ready&&s->cell_usable_mask[seg]==AMS_CELL_IMAGE_MONITORED_MASK;
  for(unsigned i=0;i<AMS_CELLS_PER_SEGMENT;i++) {
   uint16_t mv=s->cell_mv[seg][i];uint32_t age=s->cell_age_ms[seg][i];
   if(mv<500U||mv>5000U||age>2500U||(uint32_t)(now-s->publication_tick)>2500U-age)good=false;
   voltage+=(float)mv/1000.0f;
  }
  for(unsigned i=0;i<AMS_TEMP_SENSORS_PER_SEGMENT;i++) {
   uint32_t age=s->temp_age_ms[seg][i];int16_t t=s->temp_deci_c[seg][i];
   if((s->temp_usable_mask[seg]&(1UL<<i))&&age<=AMS_TEMP_STALE_MS&&
      (uint32_t)(now-s->publication_tick)<=AMS_TEMP_STALE_MS-age&&t>=-400&&t<=1200){temp+=(float)t/10.0f;nt++;}
  }
  if(good&&nt) {
   temp/=(float)nt;ams_ekf_r0_update_result_t result=AMS_EKF_R0_UPDATE_NOT_REQUESTED;
   bool ok=ams_ekf_acquisition_complete(e)?
    ams_ekf_step_gated(e,current,voltage,temp,dt,false,&result):
    ams_ekf_step_acquiring_gated(e,current,voltage,temp,dt,&result);
   ams_ekf_acquisition_observe(e,current,uncertainty,voltage,temp,s->voltage_complete_tick,c->current_trusted,ok);
  }else {e->valid=0;e->fault_flags=timing?AMS_EKF_FAULT_BAD_INPUT:AMS_EKF_FAULT_EPOCH_TIMING;}
  e->last_measurement_sequence=s->sequence;e->last_voltage_tick=s->voltage_complete_tick;
 }
 c->charge_total=s->current.total_charge_As;c->invalid_total=s->current.total_invalid_sample_count;
 c->total_seen=true;c->sequence=s->sequence;c->voltage_tick=s->voltage_complete_tick;
 if(c->accepted_epochs!=UINT32_MAX)c->accepted_epochs++;
 return true;
}
