#include <ams_core/ams_temp_image.h>
#include <ams_core/ams_thermistor.h>
#include <math.h>
#include <stddef.h>
#include <limits.h>
void ams_temp_image_apply(ams_temp_image_t *im, const int16_t raw[AMS_TEMP_COUNT],
 uint32_t received, const uint32_t captured[AMS_TEMP_COUNT], uint32_t now)
{
 if (!im || !raw || !captured) return;
 im->fresh_mask=im->usable_mask=im->stale_mask=im->invalid_mask=0U;
 im->open_mask=im->short_mask=im->jump_mask=im->rate_mask=0U;
 for (unsigned i=0; i<AMS_TEMP_COUNT; ++i) {
  uint32_t bit=1UL<<i;
  bool accepted=false;
  if (received & bit) {
   thermistor_result_t t=thermistor_from_adbms_raw(raw[i], THERMISTOR_NOMINAL_VREG_V);
   im->raw[i]=raw[i];
   if (t.valid && isfinite(t.temperature_c) && t.temperature_c>=-40.0f && t.temperature_c<=150.0f) {
    int16_t v=(int16_t)lroundf(t.temperature_c*10.0f);
    if (im->valid_mask & bit) {
     int32_t d=(int32_t)v-im->deci_c[i]; if(d<0) d=-d;
     uint32_t dt=captured[i]-im->last_update_ms[i];
     if ((uint32_t)d>=AMS_TEMP_JUMP_DECI_C) im->jump_mask|=bit;
     if(dt && ((uint32_t)d*1000U/dt)>=AMS_TEMP_RATE_DECI_C_PER_S) im->rate_mask|=bit;
    }
    if(im->filter_valid_mask & bit) {
     int32_t n=7*(int32_t)im->filtered_deci_c[i]+v;
     im->filtered_deci_c[i]=(int16_t)((n+(n>=0?4:-4))/8);
    } else im->filtered_deci_c[i]=v;
    im->filter_valid_mask|=bit; im->valid_mask|=bit; im->fresh_mask|=bit;
    im->deci_c[i]=v; im->last_update_ms[i]=captured[i]; im->misses[i]=0; accepted=true;
   } else {
    im->valid_mask&=~bit; im->filter_valid_mask&=~bit; im->invalid_mask|=bit;
    if(t.status==THERMISTOR_STATUS_OPEN_CIRCUIT) im->open_mask|=bit;
    if(t.status==THERMISTOR_STATUS_SHORT_CIRCUIT) im->short_mask|=bit;
   }
  }
  if(!accepted && im->misses[i]<UINT8_MAX) ++im->misses[i];
  if((im->valid_mask&bit) && (uint32_t)(now-im->last_update_ms[i])<=AMS_TEMP_STALE_MS && im->misses[i]<=AMS_TEMP_MAX_MISSES) im->usable_mask|=bit;
  else im->stale_mask|=bit;
 }
 if(im->usable_mask==AMS_TEMP_ALL_MASK) im->startup_scan_complete=true;
}
