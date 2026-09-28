#include <ams_core/ams_z021_support.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void packet(uint8_t p[8], uint8_t counter) {
 uint16_t pec=ams_adbms_pec10(p,counter);p[6]=(uint8_t)((counter<<2)|(pec>>8));p[7]=(uint8_t)pec;
}
static void ready(ams_z021_ring_t *r, unsigned counter) {
 ams_z021_ring_reset(r);for(unsigned i=0;i<5;i++){r->smb[i].known=true;r->smb[i].expected=(uint8_t)counter;}
}
static void packets(uint8_t s[8],uint8_t v[8],uint8_t f[8],uint8_t counter,uint16_t conversion) {
 memset(s,0,8);memset(v,0,8);memset(f,0,8);s[1]=0x40;
 v[0]=0x10;v[1]=0x27;v[4]=0x10;v[5]=0x27;
 f[2]=(uint8_t)(conversion>>6);f[3]=(uint8_t)(conversion<<2);
 packet(s,counter);packet(v,counter);packet(f,counter);
}
int main(void) {
 unsigned b;
 for(unsigned p=0;p<6;p++){
  assert(ams_z021_block_index(AMS_Z021_FROM_A,p,false,&b)&&b==p);
  assert(ams_z021_block_index(AMS_Z021_FROM_A,p,true,&b)&&b==5-p);
  assert(ams_z021_block_index(AMS_Z021_FROM_B,p,false,&b)&&b==5-p);
  assert(ams_z021_block_index(AMS_Z021_FROM_B,p,true,&b)&&b==p);
 }
 assert(!ams_z021_block_index(AMS_Z021_FROM_A,6,false,&b)&&b==6);
 assert(!ams_z021_block_index((ams_z021_direction_t)2,0,false,&b));
 assert(!ams_z021_block_index(AMS_Z021_FROM_A,0,false,NULL));
 ams_z021_ring_t r;uint64_t ticket,old;
 for(unsigned cc=0;cc<64;cc++) {
  ready(&r,cc);assert(ams_z021_ring_awake(&r,0x3f,UINT32_MAX-9,20));
  assert(ams_z021_ring_begin(&r,0,&ticket)&&r.cleanup_required&&!r.awake_token);
  assert(!ams_z021_ring_finish(&r,ticket+1U,true,true)&&r.in_flight&&r.cleanup_required);
  old=ticket;assert(!ams_z021_ring_begin(&r,1,&ticket));
  assert(ams_z021_ring_finish(&r,old,true,true)&&!r.cleanup_required);
  unsigned expected=cc;for(unsigned i=0;i<3;i++)expected=expected>=63?1:expected+1;
  for(unsigned i=0;i<5;i++)assert(r.smb[i].known&&r.smb[i].expected==expected);
  assert(!ams_z021_ring_finish(&r,old,true,true));
 }
 for(unsigned mask=0;mask<64;mask++){
  ready(&r,1);assert(ams_z021_ring_awake(&r,(uint8_t)mask,0,10)==(mask==63));
 }
 ready(&r,1);assert(ams_z021_ring_awake(&r,63,0,10));assert(!ams_z021_ring_begin(&r,10,&ticket));
 assert(!ams_z021_ring_begin(&r,1,&ticket));
 ready(&r,1);assert(!ams_z021_ring_awake(&r,63,0,0));assert(!ams_z021_ring_awake(&r,63,0,UINT32_MAX));
 ready(&r,1);assert(ams_z021_ring_awake(&r,63,0,10));assert(ams_z021_ring_begin(&r,0,&ticket));
 old=ticket;ams_z021_ring_interrupt(&r);assert(r.cleanup_required);
 assert(!ams_z021_ring_finish(&r,old,true,true));assert(!ams_z021_ring_cleanup(&r,old,true));
 assert(!ams_z021_ring_cleanup(&r,r.generation,false));assert(r.cleanup_required);
 assert(ams_z021_ring_cleanup(&r,r.generation,true));assert(!ams_z021_ring_awake(&r,63,0,10));
 for(unsigned i=0;i<5;i++)assert(!r.smb[i].known);
 for(unsigned clean=0;clean<2;clean++) {
  ready(&r,63);assert(ams_z021_ring_awake(&r,63,0,10));assert(ams_z021_ring_begin(&r,0,&ticket));
  assert(!ams_z021_ring_finish(&r,ticket,false,clean!=0));assert(r.cleanup_required==(clean==0));
  for(unsigned i=0;i<5;i++)assert(!r.smb[i].known);
 }
 ready(&r,1);r.generation=UINT64_MAX;assert(!ams_z021_ring_awake(&r,63,0,10));
 uint8_t s[8],v[8],f[8];ams_z021_calibration_t c=ams_z021_calibration_der();
 ams_z021_apm_history_t h={0};ams_z021_apm_sample_t out;
 packets(s,v,f,0,1);
 assert(ams_z021_apm_decode(&h,&c,s,v,f,0,true,false,100,&out));
 assert(out.valid&&out.current_valid&&!out.voltage_valid&&fabsf(out.current_a-100)<0.001f);
 assert(out.current_raw==10000&&out.voltage_raw==10000&&fabsf(out.voltage_v-3622.0f/22.0f)<0.001f);
 assert(!ams_z021_apm_decode(&h,&c,s,v,f,0,true,true,101,&out)&&!out.valid);
 h=(ams_z021_apm_history_t){true,2047};packets(s,v,f,63,0);
 assert(ams_z021_apm_decode(&h,&c,s,v,f,63,true,true,0,&out)&&out.voltage_valid);
 h=(ams_z021_apm_history_t){0};assert(!ams_z021_apm_decode(&h,&c,s,v,f,63,true,true,0,&out));
 for(unsigned which=0;which<3;which++)for(unsigned bit=0;bit<64;bit++){
  packets(s,v,f,1,1);uint8_t *p=which==0?s:(which==1?v:f);p[bit/8]^=(uint8_t)(1U<<(bit%8));h=(ams_z021_apm_history_t){0};
  assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out)&&!out.valid&&!h.conversion_seen);
 }
 for(unsigned cc=0;cc<64;cc++) {
  packets(s,v,f,(uint8_t)cc,1);h=(ams_z021_apm_history_t){0};
  assert(ams_z021_apm_decode(&h,&c,s,v,f,(uint8_t)cc,true,true,0,&out));
  packet(v,(uint8_t)((cc+1)%64));h=(ams_z021_apm_history_t){0};
  assert(!ams_z021_apm_decode(&h,&c,s,v,f,(uint8_t)cc,true,true,0,&out));
 }
 const uint32_t invalid_i[]={0x03ffff,0xfc0000};
 for(unsigned i=0;i<2;i++){
  packets(s,v,f,1,1);v[0]=(uint8_t)invalid_i[i];v[1]=(uint8_t)(invalid_i[i]>>8);v[2]=(uint8_t)(invalid_i[i]>>16);packet(v,1);h=(ams_z021_apm_history_t){0};
  assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out));
  packets(s,v,f,1,1);v[4]=i?0:255;v[5]=i?128:127;packet(v,1);
  assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out));
 }
 packets(s,v,f,1,1);h=(ams_z021_apm_history_t){0};
 assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,false,true,0,&out));
 s[1]=0;packet(s,1);assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out));
 packets(s,v,f,1,1);c.gain=NAN;assert(!ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out));
 c=ams_z021_calibration_eval();c.polarity=-1;v[0]=0xf0;v[1]=0xd8;v[2]=0xff;packet(v,1);
 assert(ams_z021_apm_decode(&h,&c,s,v,f,1,true,true,0,&out));assert(out.current_raw==-10000&&fabsf(out.current_a-200)<0.001f);
 puts("PASS Z021 portable support: six-device A/B ordering, one-use awake tickets, shared counters, cleanup debt, APM PEC/coherence/scaling/sentinels");
}
