#!/usr/bin/env python3
"""Z020 composite; report missing Clang/target/physical evidence explicitly."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time,shutil

def main():
 ap=argparse.ArgumentParser();ap.add_argument('repo',nargs='?',default='.');ap.add_argument('--require-clang',action='store_true');args=ap.parse_args()
 r=Path(args.repo).resolve();e=r/'docs/migration/evidence';t=time.monotonic();stages=[]
 cmds=[('Z020 source',[sys.executable,'scripts/check_z020_contract.py','.']),('Z020 mutations',[sys.executable,'scripts/check_z020_mutations.py','.']),('Z020 planner and owner',['make','-C','tests/unit/adbms_monitor','balance-shadow','adapter-z020','balance-shadow-sanitize','balance-shadow-analyze']),('Inherited Z019 campaign',[sys.executable,'scripts/run_z019_host_validation.py','.'])]
 if shutil.which('clang'): cmds.insert(3, ('Z020 Clang analyzer',['make','-C','tests/unit/adbms_monitor','balance-shadow-clang']))
 with (e/'Z020_HOST.log').open('w') as log:
  for name,cmd in cmds:
   print(name,flush=True);log.write('\n'+name+'\n');log.flush();started=time.monotonic()
   cp=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
   stages.append(dict(name=name,returncode=cp.returncode,seconds=round(time.monotonic()-started,3)))
   if cp.returncode:break
 inherited_path=e/'Z019_HOST_REPORT.json';inherited=json.loads(inherited_path.read_text())
 passed=len(stages)==len(cmds) and all(s['returncode']==0 for s in stages) and inherited['executed_tests_passed']
 report=dict(executed_tests_passed=passed,full_host_closeout=passed and inherited['full_host_closeout'],
  missing_evidence=inherited['missing_evidence'],target_build_performed=False,hardware_validation_performed=False,
  stages=stages,inherited_stage_count=len(inherited['stages']),seconds=round(time.monotonic()-t,3),
  inherited_report_sha256=hashlib.sha256(inherited_path.read_bytes()).hexdigest(),
  source_sha256=inherited['source_sha256'])
 (e/'Z020_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS executed Z020 campaign' if passed else 'FAIL Z020 campaign')
 return 0 if passed and (not args.require_clang or report['full_host_closeout']) else 1
if __name__=='__main__':sys.exit(main())
