#include <ams_core/ams_segment_fault.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
 ams_cell_image_t c={0};ams_temp_image_t t={0};ams_segment_fault_t v={0},temp={0};
 c.usable_mask=c.updated_mask=0x7fff;
 for(unsigned i=0;i<15;i++){c.raw_valid[i]=true;c.raw_mv[i]=4000;}
 ams_segment_voltage_fault(&v,&c,true,false,0,0,0);assert(v.valid&&!v.confirmed);
 for(unsigned i=1;i<=3;i++){
  ams_segment_voltage_fault(&v,&c,false,false,0,0,0);
  assert(v.read_streak==i && v.read_pending==(i<3) && v.confirmed==(i==3));
 }
 const uint16_t mv[]={4150,4180,4200,4250,3000,2800,2500,2300};
 const ams_segment_fault_reason_t reason[]={AMS_SEG_OV_WARN,AMS_SEG_CHARGE_STOP,AMS_SEG_OV_HARD,AMS_SEG_OV_SEVERE,AMS_SEG_UV_WARN,AMS_SEG_UV_SOFT,AMS_SEG_UV_HARD,AMS_SEG_UV_SEVERE};
 for(unsigned i=0;i<8;i++){
  memset(&v,0,sizeof(v));c.raw_mv[0]=mv[i];ams_segment_voltage_fault(&v,&c,true,false,0,0,0);
  assert(v.reason==reason[i]);assert(v.latched==(i==2||i==3||i==6||i==7));
 }
 c.raw_mv[0]=4000;ams_segment_voltage_fault(&v,&c,true,false,0,0,0);assert(v.latched && !v.confirmed);
 memset(&v,0,sizeof(v));c.raw_mv[0]=4190;ams_segment_voltage_fault(&v,&c,true,true,1,0,0);assert(!v.hw_disagreement);
 c.raw_mv[0]=4180;ams_segment_voltage_fault(&v,&c,true,true,1,0,0);assert(v.hw_disagreement==1);
 c.raw_mv[0]=4000;ams_segment_voltage_fault(&v,&c,true,true,0x8000,0x8000,0);assert(!v.warning);
 ams_segment_voltage_fault(&v,&c,true,false,0,0,2501);assert(v.read_fault);
 t.startup_scan_complete=true;t.usable_mask=0xffffff;
 for(unsigned i=0;i<24;i++)t.deci_c[i]=250;
 ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.valid&&!temp.warning);
 t.deci_c[0]=600;
 for(unsigned i=1;i<=20;i++){
  ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.pending_ms==100*i && temp.latched==(i==20));
 }
 t.deci_c[0]=250;ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.latched&&!temp.confirmed&&!temp.pending);
 memset(&temp,0,sizeof(temp));t.deci_c[0]=600;
 ams_segment_temperature_fault(&temp,&t,true,0);t.deci_c[0]=650;
 ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.pending_ms==100 && temp.pending_reason==AMS_SEG_HOT_SEVERE);
 ams_segment_temperature_fault(&temp,&t,false,0);assert(!temp.pending && temp.read_fault && temp.fan_max);
 t.deci_c[0]=450;ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.charge_stop&&!temp.fan_max);
 t.deci_c[0]=500;ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.charge_stop&&temp.fan_max);
 t.deci_c[0]=0;ams_segment_temperature_fault(&temp,&t,true,0);assert(temp.reason==AMS_SEG_COLD_CHARGE);
 ams_segment_temperature_fault(&temp,&t,true,12001);assert(!temp.valid);
 puts("PASS Z023 segment policy: OV/UV priority/latch/read grace/comparators, hot debounce/reset/latch, thermal warnings");
}
