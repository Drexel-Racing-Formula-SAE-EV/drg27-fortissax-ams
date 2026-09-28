#!/usr/bin/env python3
"""Compile every portable production TU with strict diagnostics and GCC analysis."""
from pathlib import Path
import subprocess,sys,tempfile,json,hashlib
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
e=r/'docs/migration/evidence';results=[]
with tempfile.TemporaryDirectory(prefix='portable-review-') as td, (e/'PORTABLE_REVIEW_ANALYZER.log').open('w') as log:
 for i,p in enumerate(sorted((r/'lib/ams_core').rglob('*.c'))):
  cmd=['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-fanalyzer','-I'+str(r/'lib/ams_core/include'),'-c',str(p),'-o',str(Path(td)/f'{i}.o')]
  q=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
  results.append({'file':str(p.relative_to(r)),'returncode':q.returncode,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(e/'PORTABLE_REVIEW_ANALYZER_REPORT.json').write_text(json.dumps({'results':results,'pass':all(x['returncode']==0 for x in results)},indent=2)+'\n')
print('Portable analyzer:',sum(x['returncode']==0 for x in results),'/',len(results))
sys.exit(0 if all(x['returncode']==0 for x in results) else 1)
