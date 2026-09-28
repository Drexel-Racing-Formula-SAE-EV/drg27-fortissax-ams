#!/usr/bin/env python3
"""Compile each unsafe change and require rejection by production-behavior tests."""
from pathlib import Path
import shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
core='lib/ams_core/faults/ams_supervision.c';adapter='drivers/ams/supervision_zephyr.c'
policy='lib/ams_core/faults/ams_segment_fault.c';current='drivers/ams/measurement_pipeline_zephyr.c'
cases=[
 ('drivers/ams/adbms_monitor_zephyr.c','supervision_work = !monitor.recovery.terminal;',
  'supervision_work = true;','hooks'),
 (core,'r->sequence==UINT64_MAX','false','test'),
 (core,'r->sequence<=s->actor[actor].sequence','false','test'),
 (core,'{200U,3000U,3000U}','{201U,3000U,3000U}','test'),
 (core,'r->history|=faults','r->history=faults','test'),
 (core,'if(s->positions&(1U<<position))return false;','if(false)return false;','test'),
 (core,'s->voltage.valid=false;','(void)s->voltage.valid;','integration'),
 (core,'s->inhibit=true;','s->inhibit=false;','integration'),
 (policy,'max>=4200U','max>=4201U','test'),
 (policy,'pending+100U','pending+200U','test'),
 (adapter,'|| !pending','|| false','integration'),
 (adapter,'if(ams_supervision_commit(&decision,a,&records[a],now))',
          'if(ams_supervision_commit(&decision,a,&records[a],now) || true)','integration'),
 (adapter,'memset(&scan,0,sizeof(scan));temp_complete=false;',
          'temp_complete=false;','integration'),
 (current,'if(!ams_z023_owner(AMS_SUP_CURRENT))return;','if(false)return;','integration'),
 (current,'ams_z023_current_complete(faults);','(void)faults;','integration'),
 ('app/src/ams_z023_safety.c','ams_bms_ok_force_low_direct();','(void)decision;','integration'),
]
with tempfile.TemporaryDirectory(prefix='z023-behavior-') as td:
 d=Path(td)
 for top in ['app','lib','drivers','include','tests']:
  shutil.copytree(r/top,d/top,ignore=shutil.ignore_patterns('__pycache__','*.o','*_sanitize'))
 baseline=subprocess.run(['make','-C','tests/unit/z023','test','integration'],cwd=d,capture_output=True,text=True)
 if baseline.returncode:raise SystemExit('FAIL mutation baseline\n'+baseline.stdout+baseline.stderr)
 for index,(name,old,new,target) in enumerate(cases,1):
  p=d/name;s=p.read_text();assert old in s,(name,old);p.write_text(s.replace(old,new))
  try:
   cp=subprocess.run(['make','-C','tests/unit/z023',target],cwd=d,capture_output=True,text=True)
  finally:p.write_text(s)
  output=cp.stdout+cp.stderr
  if not cp.returncode or 'Assertion' not in output or 'error:' in output:
   raise SystemExit('FAIL mutation was not behaviorally rejected: '+old+'\n'+output)
  print(f'PASS behavioral mutant {index}/{len(cases)}: {old}',flush=True)
print(f'PASS Z023 behavioral mutations: {len(cases)}/{len(cases)} rejected after compilation')
