#!/usr/bin/env python3
"""Z016 additions plus the complete inherited host gate; no target claim."""
import subprocess,sys
from pathlib import Path
repo=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
commands=[
 [sys.executable,str(repo/'scripts/check_z016_link_contract.py'),str(repo)],
 ['make','-C',str(repo/'tests/unit/adbms_link'),'clean','test','asan','adapter','probe'],
 ['make','-C',str(repo/'tests/unit/adbms_link'),'adapter','probe','EXTRA=-fsanitize=address,undefined -fno-omit-frame-pointer'],
 [sys.executable,str(repo/'scripts/check_z016_mutations.py'),str(repo)],
 [sys.executable,str(repo/'scripts/run_z015_host_validation.py'),str(repo)],
]
for command in commands: subprocess.run(command,check=True)
print('PASS: Z016 additions and inherited host gate; inspect inherited report for environment skips')
