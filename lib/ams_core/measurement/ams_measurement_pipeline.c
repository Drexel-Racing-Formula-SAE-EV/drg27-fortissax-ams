#include <ams_core/ams_measurement_pipeline.h>
#include <math.h>
#include <string.h>
bool ams_pipeline_init(ams_measurement_pipeline_t *p,const ams_pipeline_clock_lock_t *o,
 const ams_measurement_lock_ops_t *m)
{
 if (!p || !o || !o->lock || !o->unlock || !o->now_ms || !m) return false;
 memset(p,0,sizeof(*p));atomic_init(&p->dropped_current,false);p->ops=*o;
 if (!ams_measurement_store_init(&p->store,m) || !o->lock(o->context)) return false;
 ams_current_window_init(&p->current,o->now_ms(o->context));o->unlock(o->context);
 p->initialized=true;return true;
}
bool ams_pipeline_current(ams_measurement_pipeline_t *p,float current,float filtered,
 bool valid,bool confident,uint32_t calibration,uint16_t uncertainty,uint8_t range)
{
 if (!p || !p->initialized) return false;
 if (!p->ops.lock(p->ops.context)) { atomic_store(&p->dropped_current,true);return false; }
 uint32_t now=p->ops.now_ms(p->ops.context);
 ams_current_window_set_sensor_metadata(&p->current,uncertainty,range);
 ams_current_window_update(&p->current,now,current,filtered,
     valid&&!atomic_exchange(&p->dropped_current,false)&&isfinite(current)&&isfinite(filtered),confident,calibration);
 p->ops.unlock(p->ops.context);return true;
}
bool ams_pipeline_boundary(ams_measurement_pipeline_t *p)
{
 if (!p || !p->initialized) return false;
 p->boundary_ready=false;
 if (!p->ops.lock(p->ops.context)) return false;
 p->boundary=p->ops.now_ms(p->ops.context);
 if(atomic_exchange(&p->dropped_current,false))
  ams_current_window_update(&p->current,p->boundary,0.0f,0.0f,false,false,0U);
 memset(&p->completed,0,sizeof(p->completed));
 (void)ams_current_window_rotate(&p->current,p->boundary,&p->completed);
 p->ops.unlock(p->ops.context);p->boundary_ready=true;return true;
}
ams_sequence_t ams_pipeline_publish_single(ams_measurement_pipeline_t *p,
 const ams_cell_image_t *c,const ams_temp_image_t *t,bool coherent,uint32_t start)
{
 if (!p || !p->initialized || !p->boundary_ready) return 0;
 p->boundary_ready=false;
 ams_measurement_snapshot_t *s=ams_measurement_store_begin_write(&p->store);
 if (!s) return 0;
 ams_measurement_snapshot_reset(s);
 uint32_t now=p->ops.now_ms(p->ops.context);
 s->acquisition_start_tick=start;s->voltage_complete_tick=p->boundary;s->publication_tick=now;
 s->current=p->completed;
 if (s->current.valid) s->validity_flags|=AMS_MEAS_VALID_CURRENT;
 for(unsigned seg=0;seg<AMS_PHYSICAL_SEGMENT_COUNT;seg++) {
  for(unsigned i=0;i<AMS_CELLS_PER_SEGMENT;i++)s->cell_age_ms[seg][i]=UINT32_MAX;
  for(unsigned i=0;i<AMS_TEMP_SENSORS_PER_SEGMENT;i++)s->temp_age_ms[seg][i]=UINT32_MAX;
 }
 if (coherent && c && t) {
  if(p->balance_zero_verified)s->validity_flags|=AMS_MEAS_BALANCE_RECOVERED;
  for(unsigned i=0;i<AMS_CELLS_PER_SEGMENT;i++) {
   uint32_t age=now-c->last_update_ms[i];uint16_t bit=(uint16_t)(1U<<i);
   s->cell_mv[0][i]=c->raw_mv[i];s->cell_age_ms[0][i]=age;
   if((c->usable_mask&bit)&&c->raw_valid[i]&&age<=AMS_CELL_IMAGE_STALE_TIMEOUT_MS&&
      c->consecutive_misses[i]<=AMS_CELL_IMAGE_MAX_CONSEC_MISSES&&
      c->raw_mv[i]>=AMS_CELL_IMAGE_VALID_MIN_MV&&c->raw_mv[i]<=AMS_CELL_IMAGE_VALID_MAX_MV) {
    s->cell_usable_mask[0]|=bit;
    s->cell_avg8_mv[0][i]=c->avg8_mv[i];s->cell_iir_mv[0][i]=c->iir_mv[i];
    s->cell_avg8_usable_mask[0]|=c->avg8_usable_mask&bit;
    if(c->iir_ready)s->cell_iir_usable_mask[0]|=c->iir_usable_mask&bit;
   }
  }
  for(unsigned i=0;i<AMS_TEMP_SENSORS_PER_SEGMENT;i++) {
   uint32_t age=now-t->last_update_ms[i],bit=1UL<<i;
   s->temp_deci_c[0][i]=t->deci_c[i];s->temp_age_ms[0][i]=age;
   if((t->usable_mask&t->valid_mask&bit)&&age<=AMS_TEMP_STALE_MS&&t->misses[i]<=AMS_TEMP_MAX_MISSES&&
      t->deci_c[i]>=-400&&t->deci_c[i]<=1500)s->temp_usable_mask[0]|=bit;
  }
 }
 return ams_measurement_store_publish(&p->store,s);
}
bool ams_pipeline_estimator_eligible(const ams_measurement_snapshot_t *s,uint32_t now)
{
 if(!s||!s->sequence||(now-s->publication_tick)>AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS||
    (now-s->voltage_complete_tick)>AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS||
    (now-s->current.latest_sample_tick)>AMS_CURRENT_WINDOW_MAX_SAMPLE_AGE_MS||
    s->current.end_tick!=s->voltage_complete_tick||
    (s->validity_flags&(AMS_MEAS_VALID_VOLTAGE|AMS_MEAS_VALID_TEMPERATURE|AMS_MEAS_VALID_CURRENT))!=
    (AMS_MEAS_VALID_VOLTAGE|AMS_MEAS_VALID_TEMPERATURE|AMS_MEAS_VALID_CURRENT)||
    !s->current.valid||!s->current.calibration_record_confident||!s->current.calibration_id||
    s->current.uncertainty_mA==0U||s->current.uncertainty_mA==AMS_CURRENT_UNCERTAINTY_UNKNOWN||
    !isfinite(s->current.average_A)||!isfinite(s->current.charge_As))return false;
 for(unsigned seg=0;seg<AMS_PHYSICAL_SEGMENT_COUNT;seg++) {
  if(s->cell_usable_mask[seg]!=AMS_CELL_IMAGE_MONITORED_MASK||s->temp_usable_mask[seg]!=AMS_TEMP_ALL_MASK)return false;
  for(unsigned i=0;i<AMS_CELLS_PER_SEGMENT;i++)
   if(s->cell_age_ms[seg][i]>AMS_CELL_IMAGE_STALE_TIMEOUT_MS||s->cell_mv[seg][i]<500U||s->cell_mv[seg][i]>5000U)return false;
  for(unsigned i=0;i<AMS_TEMP_SENSORS_PER_SEGMENT;i++)
   if(s->temp_age_ms[seg][i]>AMS_TEMP_STALE_MS||s->temp_deci_c[seg][i]<-400||s->temp_deci_c[seg][i]>1500)return false;
 }
 return true;
}
