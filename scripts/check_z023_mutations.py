#!/usr/bin/env python3
from pathlib import Path
import contextlib,io,shutil,sys,tempfile
from check_z023_contract import source_check,REQUIRED
root=Path(sys.argv[1]).resolve();source_check(root)
cases=[(p,t,'/* removed Z023 invariant */') for p,tokens in REQUIRED.items() for t in tokens]
cases += [('app/z023_supervision_validation.conf','CONFIG_AMS_'+n+'=n','CONFIG_AMS_'+n+'=y') for n in ['BMS_AUTHORITY','BALANCE_AUTHORITY']]
with tempfile.TemporaryDirectory(prefix='z023-mutations-') as temp:
 d=Path(temp)
 for top in ['app','lib','drivers']:shutil.copytree(root/top,d/top)
 for path,old,new in cases:
  p=d/path;s=p.read_text();assert old in s
  p.write_text(s.replace(old,new));rejected=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):
    try:source_check(d)
    except SystemExit as e:rejected=e.code not in (None,0)
  finally:p.write_text(s)
  if not rejected:raise SystemExit('FAIL surviving mutation '+old)
print(f'PASS Z023 source negative controls: {len(cases)}/{len(cases)} rejected')
