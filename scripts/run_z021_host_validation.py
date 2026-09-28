#!/usr/bin/env python3
"""Portable Z021 evidence; deliberately cannot report full stage completion."""
from pathlib import Path
import hashlib,json,subprocess,sys,time
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve();e=r/'docs/migration/evidence';started=time.monotonic();stages=[]
cmds=[('Z021 containment',[sys.executable,'scripts/check_z021_contract.py','.']),('Z021 portable production tests',['make','-C','tests/unit/z021','test','sanitize','analyze']),('Z021 behavioral mutations',[sys.executable,'scripts/check_z021_mutations.py','.']),('Inherited Z020 campaign',[sys.executable,'scripts/run_z020_host_validation.py','.'])]
with (e/'Z021_HOST.log').open('w') as log:
 for name,cmd in cmds:
  print(name,flush=True);log.write('\n'+name+'\n');log.flush();cp=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT);stages.append({'name':name,'returncode':cp.returncode})
  if cp.returncode:break
passed=len(stages)==len(cmds) and all(s['returncode']==0 for s in stages)
report={'executed_tests_passed':passed,'z021_complete':False,'target_integrated':False,'hardware_validation_performed':False,'stages':stages,'seconds':round(time.monotonic()-started,3),'missing':['New-board definition and reviewed mixed-ring profile','Mixed-device target owner, initialization and recovery integration','APM redundant measurement path','Clang analysis and target builds','Physical mixed-ring validation'],'source_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for top in ['app','drivers','include','lib'] for p in sorted((r/top).rglob('*')) if p.is_file()}}
(e/'Z021_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS executed Z021 foundation campaign' if passed else 'FAIL Z021 campaign');sys.exit(0 if passed else 1)
