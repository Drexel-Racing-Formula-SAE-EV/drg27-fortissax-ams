#!/usr/bin/env python3
"""Behavioral negative controls: compile mutated production code and run tests."""
from pathlib import Path
import shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();path='lib/ams_core/adbms/ams_z021_support.c'
cases=[
 ('*block = write ? AMS_Z021_DEVICES - 1U - read : read;','*block = read;'),
 ('mask != AMS_Z021_FULL_RING_MASK','false'),
 ('r->awake_token = false;\n    if (ticket','r->awake_token = true;\n    if (ticket'),
 ('(uint32_t)(now - r->awake_at_us) >= r->awake_budget_us','false'),
 ('ticket != r->generation','false'),
 ('if (unsnap) r->cleanup_required = false;','r->cleanup_required = false;'),
 ('n<3U','n<2U'),
 ('ams_adbms_counter_unknown(&r->smb[i]);','r->smb[i].known = true;'),
 ('generation != r->generation','false'),
 ('(s.data[1] & 0x40U) == 0U','false'),
 ('raw == 0x03ffffU','false'),('raw == 0xfc0000U','false'),
 ('vb == 0x7fffU','false'),('vb == 0x8000U','false'),
 ('count == h->last_conversion','false'),('count == 0U','false'),
 ('expected > 63U || !unsnap','expected > 63U'),
 ('out->voltage_valid = dividers;','out->voltage_valid = true; (void)dividers;'),
 ('(int32_t)raw - 0x1000000','(int32_t)raw'),
]
subprocess.run(['make','-s','-C',str(r/'tests/unit/z021'),'test'],check=True)
with tempfile.TemporaryDirectory(prefix='z021-mutations-') as td:
 d=Path(td);shutil.copytree(r/'lib',d/'lib');shutil.copytree(r/'tests/unit/z021',d/'tests/unit/z021')
 for old,new in cases:
  p=d/path;s=p.read_text();assert old in s,old;p.write_text(s.replace(old,new))
  try:
   # Require successful compilation so an unrelated syntax error cannot count.
   cmd=['cc','-std=c11','-I'+str(d/'lib/ams_core/include'),str(d/'tests/unit/z021/support_test.c'),str(p),str(d/'lib/ams_core/adbms/ams_adbms_protocol.c'),'-lm','-o',str(d/'mutation')]
   subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   cp=subprocess.run([str(d/'mutation')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
   if cp.returncode==0:raise SystemExit('FAIL mutation survived '+old)
  finally:p.write_text(s)
print(f'PASS Z021 behavioral mutations: {len(cases)}/{len(cases)} rejected')
