#include <ams_core/ams_balance_shadow.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "balance_oracle.inc"
static uint32_t rng = 0x20301819U;
static uint32_t random_u32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static ams_cell_image_t image(uint32_t now) {
 ams_cell_image_t c = {0}; c.usable_mask = 0x7fff;
 for(unsigned i=0;i<15;i++){c.raw_mv[i]=4100;c.raw_valid[i]=true;c.last_update_ms[i]=now;}
 return c;
}
int main(void) {
 ams_balance_shadow_t s; ams_cell_image_t c=image(100);
 ams_balance_shadow_evaluate(&c,true,100,&s);assert(s.valid && s.candidate_mask==0);
 for(unsigned i=0;i<15;i++)c.raw_mv[i]=4099;
 ams_balance_shadow_evaluate(&c,true,100,&s);assert(s.valid && !s.candidate_mask && !s.cohort_min_mv);
 c.raw_mv[0]=4099;c.raw_mv[1]=4100;c.raw_mv[2]=4120;c.raw_mv[3]=4121;
 for(unsigned i=4;i<15;i++)c.raw_mv[i]=4200;
 ams_balance_shadow_evaluate(&c,true,100,&s);assert(s.valid && s.cohort_min_mv==4100 && s.candidate_mask==0x78);
 ams_balance_shadow_evaluate(&c,false,100,&s);assert(!s.valid && !s.candidate_mask);
 ams_balance_shadow_evaluate(NULL,true,100,&s);assert(!s.valid && !s.candidate_mask);
 ams_balance_shadow_evaluate(&c,true,100,NULL);
 for(unsigned i=0;i<15;i++) {
  c=image(100);c.raw_valid[i]=false;ams_balance_shadow_evaluate(&c,true,100,&s);assert(!s.valid);
  c=image(100);c.usable_mask &= (uint16_t)~(1U<<i);ams_balance_shadow_evaluate(&c,true,100,&s);assert(!s.valid);
  c=image(100);c.consecutive_misses[i]=2;ams_balance_shadow_evaluate(&c,true,2600,&s);assert(s.valid);
  c.consecutive_misses[i]=3;ams_balance_shadow_evaluate(&c,true,2600,&s);assert(!s.valid);
  c=image(100);ams_balance_shadow_evaluate(&c,true,2601,&s);assert(!s.valid);
  c.raw_mv[i]=499;ams_balance_shadow_evaluate(&c,true,100,&s);assert(!s.valid);
  c.raw_mv[i]=5001;ams_balance_shadow_evaluate(&c,true,100,&s);assert(!s.valid);
 }
 c=image(UINT32_MAX-99U);ams_balance_shadow_evaluate(&c,true,2400,&s);assert(s.valid);
 ams_balance_shadow_evaluate(&c,true,2401,&s);assert(!s.valid);
 for(unsigned trial=0;trial<100000;trial++) {
  accumulator_t d={.smb_ready=true,.voltage_full_usable=true,.min_voltage_mv=500};uint16_t expected[1];
  uint32_t now=random_u32();c=image(now);
  for(unsigned i=0;i<15;i++){uint16_t v=(uint16_t)(500+random_u32()%4501);c.raw_mv[i]=v;d.cell_voltage_mv[0][i]=v;}
  assert(accumulator_plan_balance(&d,expected));ams_balance_shadow_evaluate(&c,true,now,&s);
  assert(s.valid && s.candidate_mask==expected[0]);
 }
 puts("PASS Z020 shadow: boundary/age/wrap/invalidation and 100000 exact FreeRTOS planner comparisons");
}
