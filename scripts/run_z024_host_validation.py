#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,sys,time,shutil
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
e=r/'docs/migration/evidence';e.mkdir(parents=True,exist_ok=True)
commands=[('Z024 portable contract',[sys.executable,'scripts/check_z024_contract.py','.']),
 ('Z024 scheduler/transport/codec',['make','-C','tests/unit/z024','test','sanitize','analyze','concurrency','tsan']),
 ('Frozen scheduler differential',[sys.executable,'scripts/run_z024_differential.py','.']),
 ('Z024 behavioral controls',[sys.executable,'scripts/check_z024_behavioral_mutations.py','.']),
 ('Inherited Z023 campaign',[sys.executable,'scripts/run_z023_host_validation.py','.'])]
start=time.monotonic();stages=[]
with (e/'Z024_HOST.log').open('w') as log:
 for name,cmd in commands:
  print(name,flush=True);log.write('\n'+name+'\n');log.flush()
  p=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
  stages.append({'name':name,'returncode':p.returncode})
  if p.returncode:break
passed=len(stages)==len(commands) and all(x['returncode']==0 for x in stages)
report={'executed_tests_passed':passed,'portable_checkpoint_complete':passed,
 'full_z024_complete':False,'runtime_can_enabled':False,'target_build_performed':False,
 'hardware_validation_performed':False,'safety_evidence_promoted':False,'stages':stages,
 'seconds':round(time.monotonic()-start,3),'tools':{n:shutil.which(n) for n in ['west','cmake','clang']},
 'remaining':['Pinned driver inspected: return-code/locking hazards require adapter mitigation and tests',
 'Complete wire/pin/timing freeze and receiver invalidity compatibility',
 'Private Zephyr CAN adapter, owner/progress integration and live bench profiles',
 'Remaining compact codecs, target contracts and actual adapter SIL',
 'Target builds and physical isolated-bus validation'],
 'source_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest()
 for top in ['app','lib','drivers','include'] for p in sorted((r/top).rglob('*')) if p.is_file()}}
(e/'Z024_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS Z024 portable checkpoint' if passed else 'FAIL Z024 checkpoint')
sys.exit(0 if passed else 1)
