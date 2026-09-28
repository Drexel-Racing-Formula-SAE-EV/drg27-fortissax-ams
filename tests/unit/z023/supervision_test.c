#include <ams_core/ams_supervision.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
 ams_supervision_t s;ams_supervision_record_t r={0};
 ams_supervision_init(&s,0);ams_supervision_evaluate(&s,2999);assert(!s.stale_mask);
 ams_supervision_evaluate(&s,3000);assert(s.stale_mask==7 && s.inhibit);
 assert(ams_supervision_next(&r,3000,AMS_SUP_INVALID));
 assert(ams_supervision_commit(&s,0,&r,3000));
 ams_supervision_evaluate(&s,3000);assert(s.accepted_mask==1 && s.sticky_faults&AMS_SUP_INVALID);
 ams_supervision_evaluate(&s,3200);assert(!(s.stale_mask&1) && !s.accepted_mask);
 ams_supervision_evaluate(&s,3201);assert(s.stale_mask&1);
 assert(!ams_supervision_commit(&s,0,&r,3000));
 assert(ams_supervision_next(&r,4000,0));assert(!ams_supervision_commit(&s,0,&r,3999));
 assert(ams_supervision_commit(&s,0,&r,4000));ams_supervision_evaluate(&s,4000);
 assert(s.sticky_faults&AMS_SUP_INVALID);
 for(unsigned a=1;a<3;a++){
  assert(ams_supervision_commit(&s,a,&r,4000));ams_supervision_evaluate(&s,7000);
  assert(!(s.stale_mask&(1U<<a)));ams_supervision_evaluate(&s,7001);assert(s.stale_mask&(1U<<a));
 }
 ams_supervision_init(&s,UINT32_MAX-50U);r=(ams_supervision_record_t){0};
 assert(ams_supervision_next(&r,UINT32_MAX-10U,0));assert(ams_supervision_commit(&s,0,&r,2));
 ams_supervision_evaluate(&s,189);assert(!(s.stale_mask&1));
 ams_supervision_evaluate(&s,190);assert(s.stale_mask&1);
 r.sequence=UINT64_MAX;assert(!ams_supervision_next(&r,0,0));
 ams_supervision_scan_t scan={0};
 for(unsigned p=0;p<8;p++){
  uint32_t mask=(1U<<p)|(1U<<(p+8))|(1U<<(p+16));
  assert(ams_supervision_scan(&scan,3,p,mask,0,0)==(p==7));
  assert(!ams_supervision_scan(&scan,3,p,mask,0,0));
 }
 assert(scan.converted==0xffffff && !scan.failed);
 assert(!ams_supervision_scan(&scan,2,0,0x10101,0,0));
 assert(!ams_supervision_scan(&scan,4,0,1,0x100,0x10000));
 assert(scan.positions==1 && scan.failed==0x100 && scan.suppressed==0x10000);
 assert(!ams_supervision_scan(&scan,4,1,2,2,0x20000));
 assert(scan.positions==1);
 puts("PASS Z023 core: replay, deadlines, wrap, exhaustion, sticky faults, scan coverage/generation/outcomes");
}
