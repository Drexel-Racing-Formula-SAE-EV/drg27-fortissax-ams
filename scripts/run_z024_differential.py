#!/usr/bin/env python3
from pathlib import Path
import re,subprocess,sys,tempfile
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
o=r/'tests/unit/z024/oracle'
h=(o/'can_tx_scheduler.h').read_text()
prototypes=re.findall(r'^(?:void|bool|uint16_t)\s+ams_\w+\([^;]*;',h,re.M)
names=[re.search(r'(ams_\w+)\(',p)[1] for p in prototypes]
with tempfile.TemporaryDirectory(prefix='z024-diff-') as d:
 d=Path(d);(d/'ext_drivers').mkdir();(d/'ext_drivers/can_tx_scheduler.h').write_text(h)
 (d/'renames.h').write_text('\n'.join('#define '+n+' oracle_'+n for n in names)+'\n')
 (d/'oracle_prototypes.h').write_text('\n'.join(re.sub(r'\b(ams_\w+)(?=\()',r'oracle_\1',p) for p in prototypes))
 flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O2','-I'+str(d),'-I'+str(r/'lib/ams_core/include')]
 subprocess.run(flags+['-include',str(d/'renames.h'),'-c',str(o/'can_tx_scheduler.c'),'-o',str(d/'oracle.o')],check=True)
 subprocess.run(flags+[str(r/'tests/unit/z024/differential_test.c'),str(r/'lib/ams_core/can/ams_can_tx_scheduler.c'),str(d/'oracle.o'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
