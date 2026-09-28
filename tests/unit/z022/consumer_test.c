#include <ams_core/ams_segment_consumer.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include <fenv.h>
#include <stdio.h>
#include <string.h>
static ams_measurement_snapshot_t snapshot(uint32_t seq,uint32_t tick,double charge) {
 ams_measurement_snapshot_t s={0};s.sequence=seq;s.publication_tick=s.voltage_complete_tick=tick;
 s.current.end_tick=s.current.latest_sample_tick=tick;s.current.valid=true;
 s.current.total_charge_As=charge;s.current.charge_As=0;s.current.average_A=0;
 s.current.uncertainty_mA=UINT16_MAX;
 s.validity_flags=AMS_MEAS_VALID_CURRENT|AMS_MEAS_BALANCE_RECOVERED;
 s.cell_usable_mask[0]=0x7fff;s.temp_usable_mask[0]=0xffffff;
 for(unsigned i=0;i<15;i++)s.cell_mv[0][i]=4200;
 for(unsigned i=0;i<24;i++)s.temp_deci_c[0][i]=250;
 return s;
}
int main(void) {
 ams_segment_consumer_t c;ams_segment_consumer_init(&c);
 ams_measurement_snapshot_t s=snapshot(1,100,0);
 assert(ams_segment_consumer_step(&c,&s,100));assert(c.segment[0].valid&&!c.current_trusted);
 for(unsigned i=1;i<5;i++)assert(!c.segment[i].valid);
 float soc=c.segment[0].soc;assert(isfinite(soc));
 assert(!ams_segment_consumer_step(&c,&s,100)&&c.accepted_epochs==1&&c.segment[0].soc==soc);
 assert(!ams_segment_consumer_step(&c,&s,201)&&!c.segment[0].valid);
 s=snapshot(3,300,0);assert(ams_segment_consumer_step(&c,&s,300)&&c.segment[0].valid);
 s=snapshot(4,400,0);s.current.total_invalid_sample_count=1;
 assert(ams_segment_consumer_step(&c,&s,400)&&!c.segment[0].valid);
 s=snapshot(5,500,0);s.current.total_invalid_sample_count=1;
 assert(ams_segment_consumer_step(&c,&s,500)&&c.segment[0].valid);
 s=snapshot(6,600,0);s.current.total_invalid_sample_count=1;s.validity_flags&=~AMS_MEAS_BALANCE_RECOVERED;
 assert(ams_segment_consumer_step(&c,&s,600)&&!c.segment[0].valid);
 s=snapshot(7,700,0);s.current.total_invalid_sample_count=1;s.temp_usable_mask[0]=0;
 assert(ams_segment_consumer_step(&c,&s,700)&&!c.segment[0].valid);
 s=snapshot(8,800,0);s.current.total_invalid_sample_count=1;s.cell_age_ms[0][0]=2500;
 assert(ams_segment_consumer_step(&c,&s,801)&&!c.segment[0].valid);
 s=snapshot(9,900,0);s.current.total_invalid_sample_count=1;s.current.total_charge_As=NAN;
 assert(ams_segment_consumer_step(&c,&s,900)&&!c.segment[0].valid);
 s=snapshot(10,1000,0);s.current.total_invalid_sample_count=1;
 assert(ams_segment_consumer_step(&c,&s,1000)&&!c.segment[0].valid);
 s=snapshot(11,1100,0);s.current.total_invalid_sample_count=1;s.current.calibration_id=4;s.current.calibration_record_confident=true;s.current.uncertainty_mA=100;
 assert(ams_segment_consumer_step(&c,&s,1100)&&c.segment[0].valid&&c.current_trusted);
 assert(!ams_segment_consumer_step(&c,&s,1201)&&!c.segment[0].valid);
 ams_segment_consumer_init(&c);s=snapshot(UINT32_MAX,UINT32_MAX-99,0);
 assert(ams_segment_consumer_step(&c,&s,s.publication_tick));s=snapshot(1,0,0);
 assert(ams_segment_consumer_step(&c,&s,0)&&c.segment[0].valid);
 ams_segment_consumer_init(&c);s=snapshot(1,100,0);
 assert(ams_segment_consumer_step(&c,&s,100));
 feclearexcept(FE_ALL_EXCEPT);s=snapshot(2,200,DBL_MAX);
 assert(ams_segment_consumer_step(&c,&s,200)&&!c.segment[0].valid);
 assert(!(fetestexcept(FE_OVERFLOW|FE_INVALID)));
 feclearexcept(FE_ALL_EXCEPT);s=snapshot(3,300,-DBL_MAX);
 assert(ams_segment_consumer_step(&c,&s,300)&&!c.segment[0].valid);
 assert(!(fetestexcept(FE_OVERFLOW|FE_INVALID)));
 ams_segment_consumer_init(&c);s=snapshot(1,100,0);s.current.average_A=FLT_MAX;
 assert(ams_segment_consumer_step(&c,&s,100)&&!c.segment[0].valid);
 puts("PASS Z022 segment consumer: real EKF, absent SMB isolation, duplicate/stale/age/wrap, invalid-current continuity, balance and temperature gating, extreme-input arithmetic");
}
