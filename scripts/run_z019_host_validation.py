#!/usr/bin/env python3
"""Z019 composite; report missing Clang/target/physical evidence explicitly."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time

def main():
 ap=argparse.ArgumentParser();ap.add_argument('repo',nargs='?',default='.');ap.add_argument('--require-clang',action='store_true');args=ap.parse_args()
 r=Path(args.repo).resolve();e=r/'docs/migration/evidence';t=time.monotonic();stages=[]
 cmds=[('Z019 source',[sys.executable,'scripts/check_z019_contract.py','.']),('Z019 negative controls',[sys.executable,'scripts/check_z019_mutations.py','.']),('Z019 owner adapter',['make','-C','tests/unit/adbms_monitor','adapter-z019']),('Z019 diagnostic owner adapter',['make','-C','tests/unit/adbms_monitor','adapter-z019-diag']),('Inherited Z018 production campaign with Z019 core tests',[sys.executable,'scripts/run_z018_host_validation.py','.'])]
 with (e/'Z019_HOST.log').open('w') as log:
  for name,cmd in cmds:
   print(name,flush=True);log.write('\n'+name+'\n');log.flush();started=time.monotonic()
   cp=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
   stages.append(dict(name=name,returncode=cp.returncode,seconds=round(time.monotonic()-started,3)))
   if cp.returncode:break
 inherited_path=e/'Z018_REBUILD_HOST_REPORT.json';inherited=json.loads(inherited_path.read_text())
 passed=len(stages)==len(cmds) and all(s['returncode']==0 for s in stages) and inherited['focused_tests_passed']
 report=dict(executed_tests_passed=passed,full_host_closeout=passed and inherited['full_host_closeout'],
  missing_evidence=inherited['blocked'],target_build_performed=False,hardware_validation_performed=False,
  stages=stages,inherited_stage_count=len(inherited['stages']),seconds=round(time.monotonic()-t,3),
  inherited_report_sha256=hashlib.sha256(inherited_path.read_bytes()).hexdigest(),
  source_sha256=inherited['source_sha256'])
 (e/'Z019_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 print('PASS executed Z019 campaign' if passed else 'FAIL Z019 campaign')
 return 0 if passed and (not args.require_clang or report['full_host_closeout']) else 1
if __name__=='__main__':sys.exit(main())
