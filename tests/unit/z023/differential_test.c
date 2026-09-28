#include <ams_core/ams_segment_fault.h>
#include "ext_drivers/voltage_fault.h"
#include "ext_drivers/temperature_fault.h"
#include <assert.h>
#include <stdio.h>
static uint32_t rng=0x230022;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void)
{
 accumulator_t a={0};ams_cell_image_t c={0};ams_temp_image_t t={0};
 voltage_fault_state_t ov;temperature_fault_state_t ot;
 ams_segment_fault_t v={0},temp={0};voltage_fault_init(&ov);temperature_fault_init(&ot);
 a.smb.ics=&a;a.smb.num_ics=1;a.smb.monitored_cell_count=15;
 a.voltage_startup_scan_complete=a.temp_startup_scan_complete=true;
 a.usable_voltage_count=a.updated_voltage_count=15;a.usable_voltage_mask[0]=0x7fff;
 c.usable_mask=c.updated_mask=0x7fff;t.usable_mask=0xffffff;t.startup_scan_complete=true;
 for(unsigned step=0;step<100000;step++){
  bool good=(random32()%10)!=0;
  a.voltage_full_updated=a.voltage_full_usable=good;
  a.min_voltage_mv=UINT16_MAX;a.max_voltage_mv=0;
  for(unsigned i=0;i<15;i++){
   uint16_t mv=(uint16_t)(2200+random32()%2200);
   c.raw_valid[i]=true;c.raw_mv[i]=a.cell_voltage_mv[0][i]=mv;
   if(mv<a.min_voltage_mv)a.min_voltage_mv=mv;
   if(mv>a.max_voltage_mv)a.max_voltage_mv=mv;
  }
  a.smb.diag[0].statd_valid=(random32()&1)!=0;
  a.smb.diag[0].cell_ov_mask=(uint16_t)random32();a.smb.diag[0].cell_uv_mask=(uint16_t)random32();
  voltage_fault_update(&ov,&a);
  ams_segment_voltage_fault(&v,&c,good,a.smb.diag[0].statd_valid,a.smb.diag[0].cell_ov_mask,a.smb.diag[0].cell_uv_mask,0);
  assert(v.valid==ov.voltage_valid && v.read_fault==ov.read_fault && v.read_pending==ov.read_fault_pending);
  assert(v.read_streak==ov.read_fault_streak && v.warning==ov.warning && v.charge_stop==ov.charge_stop);
  assert(v.confirmed==ov.confirmed && v.latched==ov.latched);
  assert(v.hw_disagreement==ov.hardware_disagreement_mask[0]);
  assert(v.software_ov==ov.software_ov_mask[0] && v.software_uv==ov.software_uv_mask[0]);
  /* Correlated heat plateaus exercise confirmation, rather than resetting
   * hot tier on every random step. */
  int16_t tc=(int16_t)(((step/25)%5)*200);
  a.max_temp_deci_c=tc;a.min_temp_deci_c=tc-20;a.temp_full_usable=good;
  for(unsigned i=0;i<24;i++)t.deci_c[i]=i?tc:tc-20;
  temperature_fault_update_with_period(&ot,&a,100);
  ams_segment_temperature_fault(&temp,&t,good,0);
  assert(temp.valid==ot.temp_valid && temp.read_fault==ot.read_fault && temp.warning==ot.warning);
  assert(temp.fan_max==ot.fan_max && temp.charge_stop==ot.charge_stop && temp.pending==ot.pending);
  assert(temp.pending_ms==ot.pending_ms && temp.confirmed==ot.confirmed && temp.latched==ot.latched);
 }
 puts("PASS Z023 100000 differential voltage/temperature policy steps against frozen v2.6.27");
}
