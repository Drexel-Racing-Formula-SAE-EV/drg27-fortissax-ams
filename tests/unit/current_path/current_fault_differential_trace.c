#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORACLE
#include <ext_drivers/current_fault.h>
#else
#include <ams_core/ams_current_fault.h>
#endif
static uint32_t r; static uint32_t rnd(void){uint32_t x=r;x^=x<<13;x^=x>>17;x^=x<<5;r=x;return x;} static uint64_t h=1469598103934665603ULL;static void hb(const void*p,size_t n){const unsigned char*b=p;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}}static void u32(uint32_t x){hb(&x,4);}static void u8(uint8_t x){hb(&x,1);}static void fl(float x){uint32_t u;memcpy(&u,&x,4);u32(u);}static void hs(const current_fault_state_t*s){u8(s->sensor_fault);u8(s->warning);u8(s->pending);u8(s->confirmed);u8(s->latched);u32(s->reason);u32(s->pending_reason);u32(s->latched_reason);u32(s->mode);u32(s->sensor_invalid_ms);u32(s->pending_ms);fl(s->abs_current_a);fl(s->threshold_a);}int main(int argc,char**argv){r=argc>1?(uint32_t)strtoul(argv[1],0,0):1u;uint32_t n=argc>2?(uint32_t)strtoul(argv[2],0,0):100000u;if(!r)r=1;current_fault_state_t s;current_fault_init(&s);hs(&s);for(uint32_t i=0;i<n;i++){uint32_t op=rnd()%20u;if(op==0){current_fault_init(&s);}else if(op==1){current_fault_reset_latch(&s);}else{current_fault_mode_t m=(current_fault_mode_t)(rnd()%4u);int32_t ma=(int32_t)(rnd()%700001u)-350000;float a=(float)ma/1000.0f;bool valid=(rnd()%10u)!=0u;current_sensor_reason_t reason=(current_sensor_reason_t)(rnd()%9u);static const uint32_t periods[]={0u,1u,20u,40u,100u,500u};uint32_t p=periods[rnd()%6u];current_fault_update(&s,m,a,valid,reason,p);}hs(&s);u32(i);}printf("%016llx\n",(unsigned long long)h);return 0;}
