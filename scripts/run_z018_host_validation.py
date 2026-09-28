#!/usr/bin/env python3
"""Reproducible Z018 focused campaign. Incomplete gates remain explicit."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, time

def main():
 ap=argparse.ArgumentParser();ap.add_argument('repo',nargs='?',default='.');ap.add_argument('--require-clang',action='store_true');args=ap.parse_args()
 r=Path(args.repo).resolve();e=r/'docs/migration/evidence';e.mkdir(parents=True,exist_ok=True)
 stages=[];start=time.monotonic();blocked=[]
 cmds=[('source '+n,[sys.executable,'scripts/'+n+'.py','.']) for n in ['check_z014_source_hygiene','check_z015_source_hygiene','check_z016_link_contract','check_z017_contract','check_z018_contract']]
 cmds += [('inherited mutations '+n,[sys.executable,'scripts/check_'+n+'_contract_mutations.py','.']) for n in ['z014','z015']]
 cmds += [('mutations '+n,[sys.executable,'scripts/check_'+n+'_mutations.py','.']) for n in ['z016','z017','z018']]
 cmds += [('monitor '+n,['make','-C','tests/unit/adbms_monitor',n]) for n in ['test','adapter','adapter-z018','adapter-z018-diag','thermistor','asan','ubsan','analyze']]
 cmds += [('Z016 link/probe',['make','-C','tests/unit/adbms_link','test','adapter','probe'])]
 if shutil.which('clang'):cmds.append(('Clang analyzer',['make','-C','tests/unit/adbms_monitor','clang-analyze']))
 else:blocked.append('Clang static analyzer unavailable')
 with (e/'Z018_REBUILD_HOST.log').open('w') as log:
  for name,cmd in cmds:
   t=time.monotonic();log.write('\n'+name+'\n');log.flush()
   cp=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
   stages.append(dict(name=name,returncode=cp.returncode,seconds=round(time.monotonic()-t,3)))
   print(('PASS ' if cp.returncode==0 else 'FAIL ')+name,flush=True)
   if cp.returncode:break
 passed=len(stages)==len(cmds) and all(s['returncode']==0 for s in stages)
 inherited=e/'Z018_INHERITED_Z015_REPORT.json';d=json.loads(inherited.read_text()) if inherited.exists() else {}
 if not d.get('success') or not d.get('thread_sanitizer_performed'):blocked.append('Inherited Z015/TSan gate incomplete')
 blocked+=d.get('skipped',[])
 report=dict(focused_tests_passed=passed,full_host_closeout=passed and not blocked,blocked=sorted(set(blocked)),
  target_build_performed=False,hardware_validation_performed=False,leak_sanitizer_performed=False,
  inherited_report_sha256=hashlib.sha256(inherited.read_bytes()).hexdigest() if inherited.exists() else None,
  source_sha256={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for top in ['app','drivers','include','lib'] for p in sorted((r/top).rglob('*')) if p.is_file()},
  stages=stages,seconds=round(time.monotonic()-start,3))
 (e/'Z018_REBUILD_HOST_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0 if passed and (not args.require_clang or not blocked) else 1
if __name__=='__main__':sys.exit(main())
