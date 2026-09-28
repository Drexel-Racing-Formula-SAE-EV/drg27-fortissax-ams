#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess,sys,time,shutil
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
e=r/'docs/migration/evidence';e.mkdir(parents=True,exist_ok=True)
commands=[('Z023 contract',[sys.executable,'scripts/check_z023_contract.py','.']),
 ('Z023 source negative controls',[sys.executable,'scripts/check_z023_mutations.py','.']),
 ('Z023 production core/adapters/hooks',['make','-C','tests/unit/z023','test','hooks','integration','integration-sanitize','differential','concurrency','sanitize','analyze']),
 ('Z023 optional diagnostics hooks',['make','-C','tests/unit/z023','hooks','DIAGNOSTICS=1']),
 ('Z023 ThreadSanitizer',['make','-C','tests/unit/z023','tsan']),
 ('Z023 behavioral negative controls',[sys.executable,'scripts/check_z023_behavioral_mutations.py','.']),
 ('Inherited Z022 campaign',[sys.executable,'scripts/run_z022_host_validation.py','.'])]
stages=[];start=time.monotonic()
with (e/'Z023_HOST.log').open('w') as log:
 for name,cmd in commands:
  print(name,flush=True);log.write('\n'+name+'\n');log.flush()
  p=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
  stages.append({'name':name,'returncode':p.returncode})
  if p.returncode:break
passed=len(stages)==len(commands) and all(s['returncode']==0 for s in stages)
report={'executed_tests_passed':passed,'full_z023_complete':False,'target_build_performed':False,
 'shadow_profile_host_sil_complete':passed,
 'hardware_validation_performed':False,'safety_evidence_promoted':False,'stages':stages,
 'seconds':round(time.monotonic()-start,3),'clang_available':bool(shutil.which('clang')),
 'remaining':['Zephyr target/profile builds and real scheduling/stack checks',
 'CMake null-platform link gate and Clang analysis (tools unavailable here)',
 'Physical gates and individual actor safety-evidence qualification'],
 'source_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest()
 for top in ['app','lib','drivers','include'] for p in sorted((r/top).rglob('*')) if p.is_file()}}
(e/'Z023_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS executed Z023 checkpoint tests' if passed else 'FAIL Z023 checkpoint')
sys.exit(0 if passed else 1)
