#!/usr/bin/env python3
"""Supplemental UBSan review, not a substitute for the full sanitizer gate."""
import hashlib,json,subprocess,sys
from pathlib import Path
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
e=r/'docs/migration/evidence'
suites=['watchdog','safety_fatal','bms_ok','current_path','fan','imd',
        'measurement_store','current_window','estimator','adbms_spi']
commands=[['make','-C','tests/unit/'+s,'ubsan'] for s in suites]
commands += [['make','-C','tests/system/z014_safety_sil','ubsan'],
             ['make','-C','tests/unit/imd','tsan']]
stages=[]
with (e/'Z013_ONWARDS_UBSAN.log').open('w') as log:
    for cmd in commands:
        log.write('\n'+ ' '.join(cmd)+'\n');log.flush()
        p=subprocess.run(cmd,cwd=r,stdout=log,stderr=subprocess.STDOUT)
        stages.append(dict(command=cmd,returncode=p.returncode))
report=dict(passed=all(s['returncode']==0 for s in stages),stages=stages,
            leak_sanitizer_performed=False,target_build_performed=False,
            source_sha256={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest()
            for top in ['app','lib','drivers','include'] for p in sorted((r/top).rglob('*')) if p.is_file()})
(e/'Z013_ONWARDS_UBSAN_REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS supplemental UBSan + IMD TSan' if report['passed'] else 'FAIL supplemental review')
sys.exit(0 if report['passed'] else 1)
