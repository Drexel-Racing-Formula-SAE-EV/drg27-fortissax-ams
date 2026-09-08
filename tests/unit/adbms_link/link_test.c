#include <ams_core/ams_adbms_link.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint16_t Pec15_Calc(uint8_t,uint8_t *);
uint16_t pec10_calc(uint8_t,int,uint8_t *);
static uint64_t clock_us;
static unsigned wakes,reads;
static bool fail_clock,fail_io,corrupt;
static uint8_t response[8]={0,6};
static bool now(void *c,uint64_t *v){(void)c;*v=clock_us;return !fail_clock;}
static bool wake(void *c,bool cold){(void)c;wakes++;clock_us+=cold?9000:2000;return !fail_io;}
static bool read_io(void *c,const uint8_t cmd[4],uint8_t rx[8]){
 (void)c;reads++;
 assert(cmd[0]==0 && (cmd[1]==2 || cmd[1]==0x2c));
 assert((((unsigned)cmd[2]<<8)|cmd[3])==Pec15_Calc(2,(uint8_t *)cmd));
 memcpy(rx,response,8);if(corrupt)rx[0]^=1;clock_us+=300;return !fail_io;
}
static void counter(unsigned n){response[6]=(uint8_t)(n<<2);uint16_t p=pec10_calc(1,6,response);response[6]|=(uint8_t)(p>>8);response[7]=(uint8_t)p;}
int main(void){
 uint32_t state=12345;uint8_t b[8];
 for(unsigned n=0;n<100000;n++) {
  for(unsigned i=0;i<8;i++){state=state*1664525U+1013904223U;b[i]=(uint8_t)(state>>24);}
  assert(ams_link_pec15(b,2)==Pec15_Calc(2,b));
  assert(ams_link_pec10(b,b[6]>>2)==pec10_calc(1,6,b));
 }
 ams_link_t l={0};ams_link_sample_t out;
 ams_link_io_t io={.now_us=now,.wake=wake,.read=read_io};
 counter(0);
 assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_SESSION_EXPIRED);
 assert(!reads && !wakes && !out.valid);
 assert(ams_link_read(&l,&io,AMS_LINK_SID,false,&out)==AMS_LINK_OK);
 assert(out.valid && wakes==1 && reads==1 && l.counter_valid);
 for(unsigned n=0;n<64;n++){
  counter(n);
  ams_link_result_t expected=n?AMS_LINK_COUNTER:AMS_LINK_OK;
  assert(ams_link_read(&l,&io,AMS_LINK_CFGA,true,&out)==expected);
  if(n) assert(!out.valid);
  assert(ams_link_read(&l,&io,AMS_LINK_CFGA,true,&out)==AMS_LINK_OK);
 }
 counter(0);assert(ams_link_read(&l,&io,AMS_LINK_CFGA,true,&out)==AMS_LINK_COUNTER);
 corrupt=true;assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_PEC);
 assert(!out.valid && !l.counter_valid && !l.session_valid);corrupt=false;
 assert(ams_link_read(&l,&io,AMS_LINK_SID,false,&out)==AMS_LINK_OK);
 clock_us+=2999;assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_OK);
 clock_us+=3000;unsigned before=reads;
 assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_SESSION_EXPIRED);
 assert(reads==before && !out.valid);
 assert(ams_link_wake(&l,&io,true)==AMS_LINK_OK);
 assert(!l.session_valid);
 assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_SESSION_EXPIRED);
 assert(ams_link_read(&l,&io,AMS_LINK_SID,false,&out)==AMS_LINK_OK);
 fail_io=true;assert(ams_link_read(&l,&io,AMS_LINK_SID,true,&out)==AMS_LINK_IO);
 assert(!l.session_valid && !out.valid);fail_io=false;
 fail_clock=true;assert(ams_link_wake(&l,&io,false)==AMS_LINK_CLOCK);fail_clock=false;
 response[1]=0;counter(0);
 assert(ams_link_read(&l,&io,AMS_LINK_SID,false,&out)==AMS_LINK_IDENTITY);
 assert(!out.valid);
 before=reads;assert(ams_link_read(&l,&io,(ams_link_read_t)42,false,&out)==AMS_LINK_INVALID);
 assert(before==reads);
 clock_us=0;assert(ams_link_wake(&l,&io,false)==AMS_LINK_OK);
 clock_us=1;assert(ams_link_read(&l,&io,AMS_LINK_CFGA,true,&out)==AMS_LINK_SESSION_EXPIRED);
 puts("PASS Z016 link: 200000 differential PEC checks; counter/session/error boundaries");
}
