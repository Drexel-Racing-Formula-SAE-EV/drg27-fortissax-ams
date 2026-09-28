#!/usr/bin/env python3
"""Z021 foundation must not create a live mixed-ring runtime implicitly."""
from pathlib import Path
import sys
from check_z017_contract import elf_symbols
r=Path(sys.argv[1]).resolve()
for top in ['app','drivers','include']:
 for p in (r/top).rglob('*'):
  if p.suffix in ['.c','.h','.conf'] or p.name=='Kconfig':
   if 'ams_z021_' in p.read_text() or 'CONFIG_AMS_Z021' in p.read_text():
    raise SystemExit('FAIL Z021: unexpected runtime integration '+str(p))
s=(r/'lib/ams_core/adbms/ams_z021_support.c').read_text()
for forbidden in ['zephyr/','stm32','write_b(', 'UNMUTE', 'GPIO']:
 if forbidden in s:raise SystemExit('FAIL Z021: portable boundary '+forbidden)
if len(sys.argv)>2:
 symbols=elf_symbols(Path(sys.argv[2]).resolve()/'zephyr/zephyr.elf')
 if any(name.startswith('ams_z021_') for name in symbols):raise SystemExit('FAIL Z021: foundation unexpectedly live in target')
print('PASS Z021 foundation containment')
