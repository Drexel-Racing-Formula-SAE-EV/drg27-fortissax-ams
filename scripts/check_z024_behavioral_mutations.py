#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
s=r/'lib/ams_core/can';source=(s/'ams_can_transport.c').read_text()
cases=[
 ('prepublication age forgotten','uint32_t queued_age=now-encoded_ms;','uint32_t queued_age=(now-encoded_ms)*0U;'),
 ('recovery proof bypass','!t->recovery_pending ||',''),
 ('new recovery fault erased','if(t->faults) { t->recovery_pending=false; unlock(t); return false; }','t->faults=0;'),
 ('cached RX expiry bypass','elapsed>100U || (t->last_rx.data[2] && t->last_rx.data[7]>100U-elapsed)','elapsed>1000U || (t->last_rx.data[2] && t->last_rx.data[7]>1000U-elapsed)'),
 ('late completion credited','(uint32_t)(e->tick-s->submitted_ms)>100U','(uint32_t)(e->tick-s->submitted_ms)>1000U'),
 ('submission credited','inc(&t->submitted);','inc(&t->submitted); inc(&t->completed);'),
 ('cookie reuse','s->cookie=++t->next_cookie;','s->cookie=1;'),
 ('overflow ignored','if(!ok) ams_can_transport_fault(t,AMS_CAN_LOSS);','if(!ok) { /* lost */ }'),
 ('lock loss ignored','ams_can_transport_fault(t,AMS_CAN_LOSS); return false;','return false;'),
 ('RX budget unbounded','n<AMS_CAN_EVENT_BUDGET','n<AMS_CAN_EVENT_CAPACITY'),
 ('remote accepted','e->remote || e->fd ||','e->fd ||'),
 ('FD accepted','e->remote || e->fd ||','e->remote ||'),
 ('stale RX accepted','(uint32_t)(now-e->tick)>100','(uint32_t)(now-e->tick)>1000'),
 ('cached fault progress','if(t->inhibited) return false;','if(t->inhibited) { ++t->service_sequence; return false; }'),
 ('ready proof bypass','!ready || !settled','(!ready && !settled)'),
 ('TX stale accepted','elapsed>100 || (f.data[2] && age>100-elapsed)','elapsed>1000 || (f.data[2] && age>1000-elapsed)'),
 ('timestamp bypass','(uint32_t)(e->tick-s->submitted_ms)>=0x80000000U','false'),
 ('timeout omitted','if(t->outstanding[i].used && (uint32_t)(now-t->outstanding[i].submitted_ms)>100)','if(false)'),
 ('failed completion accepted','if(e->success) inc(&t->completed);','if(true) inc(&t->completed);'),
 ('power ID exposed','f->id!=0x68bU','(f->id!=0x68bU && f->id!=0x680U)'),
 ('APM exposed','return f->data[0]==1 &&','return (f->data[0]==1 || f->data[0]==2) &&'),
]
with tempfile.TemporaryDirectory(prefix='z024-mutations-') as d:
 d=Path(d)
 for name,old,new in cases:
  assert old in source,name
  p=d/'mutant.c';p.write_text(source.replace(old,new,1));exe=d/'test'
  cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O1','-I'+str(r/'lib/ams_core/include'),str(r/'tests/unit/z024/transport_test.c'),str(s/'ams_can_tx_scheduler.c'),str(s/'ams_can_codec.c'),str(p),'-o',str(exe)]
  c=subprocess.run(cmd,capture_output=True,text=True)
  assert c.returncode==0, name+': compile failure does not count\n'+c.stderr
  c=subprocess.run([str(exe)],capture_output=True)
  assert c.returncode!=0,name+': survived'
  print('REJECT',name,flush=True)
print('PASS Z024 behavioral controls:',len(cases))
