#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,sys,time
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve();e=r/'docs/migration/evidence';started=time.monotonic();stages=[]
cmds=[('Z022 source',[sys.executable,'scripts/check_z022_contract.py','.']),('Z022 mutations',[sys.executable,'scripts/check_z022_mutations.py','.']),('Z022 production pipeline/adapter',['make','-C','tests/unit/z022','test','adapter','sanitize','analyze','consumer','consumer-sanitize','consumer-analyze']),('Z022 monitor boundary hook',['make','-C','tests/unit/adbms_monitor','adapter-z022']),('Current window regression',['make','-C','tests/unit/current_window','test']),('Pinned store concurrency regression',['make','-C','tests/unit/measurement_store','test']),('Inherited Z021 through Z018 campaign',[sys.executable,'scripts/run_z021_host_validation.py','.'])]
with (e/'Z022_HOST.log').open('w') as log:
 for name,cmd in cmds:
  print(name,flush=True);log.write('\n'+name+'\n');log.flush();cp=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT);stages.append({'name':name,'returncode':cp.returncode})
  if cp.returncode:break
passed=len(stages)==len(cmds) and all(s['returncode']==0 for s in stages)
report={'executed_tests_passed':passed,'z022_full_stage_complete':False,'single_smb_target_source_integrated':True,'target_build_performed':False,'hardware_validation_performed':False,'stages':stages,'seconds':round(time.monotonic()-started,3),'remaining':['Full-pack/mixed topology acquisition','Current/fault qualification and genuine safety heartbeat promotion','Clang analysis and target builds','Physical timing and migration validation'],'source_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for top in ['app','drivers','include','lib'] for p in sorted((r/top).rglob('*')) if p.is_file()}}
(e/'Z022_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS executed Z022 campaign' if passed else 'FAIL Z022');sys.exit(0 if passed else 1)
