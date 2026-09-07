#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#ifdef ORACLE
#include <ext_drivers/current_sensor.h>
#else
#include <ams_core/ams_current_sensor.h>
#endif

static uint32_t rng_state;
static uint32_t rnd(void) { uint32_t x=rng_state; x^=x<<13; x^=x>>17; x^=x<<5; rng_state=x; return x; }
static uint64_t h=1469598103934665603ULL;
static void hb(const void *p, size_t n){const unsigned char *b=p; for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}}
static void hu32(uint32_t x){hb(&x,sizeof x);} static void hu16(uint16_t x){hb(&x,sizeof x);} static void hu8(uint8_t x){hb(&x,sizeof x);} static void hf(float x){uint32_t u; memcpy(&u,&x,4);hu32(u);} static void hi32(int32_t x){hu32((uint32_t)x);} static void hi16(int16_t x){hu16((uint16_t)x);}
static void hs(const current_sensor_t *s){
#define HF(f) hf(s->f)
#define HU32(f) hu32(s->f)
#define HU16(f) hu16(s->f)
#define HBOL(f) hu8((uint8_t)(s->f?1:0))
 HF(current);HF(voltage_high);HF(voltage_low);HF(sensor_voltage_high);HF(sensor_voltage_low);HF(current_high);HF(current_low);HF(current_50a);HF(current_800a);HF(current_50a_raw);HF(current_800a_raw);HF(current_50a_filtered);HF(current_800a_filtered);HF(current_filtered);HBOL(filter_initialized);HF(zero_offset_50a);HF(zero_offset_800a);HBOL(zero_calibrated);HU32(zero_cal_count);HBOL(calibration_loaded_from_record);HU32(calibration_id);HU32(calibration_capture_time_s);hi16(s->calibration_temp_deci_c);HU16(calibration_uncertainty_50a_mA);HU16(calibration_uncertainty_800a_mA);HU32(calibration_restore_count);HF(adc_vref_v);HF(sensor_supply_v);HU16(count_high);HU16(count_low);HBOL(count_high_fresh);HBOL(count_low_fresh);HBOL(last_read_ok);HBOL(current_valid);hu32((uint32_t)s->selected_range);hu32((uint32_t)s->reason);
#undef HF
#undef HU32
#undef HU16
#undef HBOL
}
static void hr(const current_sensor_calibration_record_t *r){hu32(r->magic);hu16(r->schema);hu16(r->size);hu32(r->calibration_id);hu32(r->capture_time_s);hi32(r->zero_offset_50a_mA);hi32(r->zero_offset_800a_mA);hu32(r->adc_vref_uV);hu32(r->sensor_supply_uV);hi16(r->calibration_temp_deci_c);hu16(r->uncertainty_50a_mA);hu16(r->uncertainty_800a_mA);hu16(r->reserved);hu32(r->crc32);}
static void init_sensor(current_sensor_t *s){
#ifdef ORACLE
 static ADC_HandleTypeDef lo,hi; current_sensor_init(s,&lo,&hi,10u,3u);
#else
 current_sensor_init(s);
#endif
}
static void inject(current_sensor_t *s, uint16_t hi, uint16_t lo, int fail){
#ifdef ORACLE
 s->count_high_fresh=false;s->count_low_fresh=false;s->last_read_ok=false;
 if(fail==1){s->current_valid=false;s->selected_range=CURRENT_SENSOR_RANGE_UNKNOWN;s->reason=CURRENT_SENSOR_REASON_ADC_READ;return;}
 s->count_high=hi;s->count_high_fresh=true;
 if(fail==2){s->current_valid=false;s->selected_range=CURRENT_SENSOR_RANGE_UNKNOWN;s->reason=CURRENT_SENSOR_REASON_ADC_READ;return;}
 s->count_low=lo;s->count_low_fresh=true;s->last_read_ok=true;
#else
 current_sensor_adc_begin(s);
 if(fail==1){(void)current_sensor_adc_finish(s);return;}
 current_sensor_adc_publish_high(s,hi);
 if(fail==2){(void)current_sensor_adc_finish(s);return;}
 current_sensor_adc_publish_low(s,lo);(void)current_sensor_adc_finish(s);
#endif
}
int main(int argc,char **argv){uint32_t seed=argc>1?(uint32_t)strtoul(argv[1],0,0):1u; uint32_t n=argc>2?(uint32_t)strtoul(argv[2],0,0):100000u; rng_state=seed?seed:1u; current_sensor_t s; current_sensor_calibration_record_t rec={0}; bool have=false; init_sensor(&s); hs(&s);
 for(uint32_t i=0;i<n;i++){uint32_t op=rnd()%10u; switch(op){
 case 0:{uint16_t hi=(uint16_t)(rnd()&4095u),lo=(uint16_t)(rnd()&4095u);int fail=(int)(rnd()%20u==0u?1:(rnd()%20u==0u?2:0));inject(&s,hi,lo,fail); if(fail==0)(void)current_sensor_convert(&s);break;}
 case 1:{float vr=2.6f+(float)(rnd()%1201u)/1000.0f; float sv=4.3f+(float)(rnd()%1401u)/1000.0f; current_sensor_set_reference_voltages(&s,vr,sv);break;}
 case 2:(void)current_sensor_zero_calibrate(&s);break;
 case 3:current_sensor_zero_clear(&s);break;
 case 4:{current_sensor_calibration_metadata_t m={.calibration_id=(rnd()%8u==0u)?0u:(rnd()|1u),.capture_time_s=(rnd()%5u==0u)?0u:rnd(),.calibration_temp_deci_c=(int16_t)((int)(rnd()%1801u)-500),.uncertainty_50a_mA=(uint16_t)(rnd()%1000u),.uncertainty_800a_mA=(uint16_t)(rnd()%8000u)}; current_sensor_calibration_record_t t=rec; bool ok=current_sensor_calibration_record_create(&s,&m,&t); if(ok){rec=t;have=true;} hu8(ok);break;}
 case 5:{current_sensor_calibration_record_t t=rec;if((rnd()&3u)==0u)t.crc32^=1u;bool ok=current_sensor_calibration_record_valid(have?&t:NULL);hu8(ok);break;}
 case 6:{bool proof=(rnd()&1u)!=0u;bool ok=current_sensor_calibration_apply(&s,have?&rec:NULL,proof);hu8(ok);break;}
 case 7:{bool ok=current_sensor_calibration_confident(&s);hu8(ok);break;}
 case 8:(void)current_sensor_convert(&s);break;
 default:{/* directed near-zero acquisition occasionally keeps calibration paths observable */uint16_t hi=(uint16_t)(1845u+(rnd()%33u));uint16_t lo=(uint16_t)(1845u+(rnd()%33u));inject(&s,hi,lo,0);(void)current_sensor_convert(&s);break;}
 }
 hs(&s);hu8(have);hr(&rec);hu32(i);
 }
 printf("%016llx\n",(unsigned long long)h);return 0;}
