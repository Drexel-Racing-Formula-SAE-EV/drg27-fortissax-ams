#!/usr/bin/env python3
import shutil, subprocess, sys, tempfile
from pathlib import Path
repo=Path(sys.argv[1]).resolve()
cases=[
 ('lib/ams_core/include/ams_core/ams_adbms_link.h','AMS_ADBMS_LINK_STRING 1U','AMS_ADBMS_LINK_STRING 0U'),
 ('lib/ams_core/include/ams_core/ams_adbms_link.h','AMS_ADBMS_LINK_IC_COUNT 1U','AMS_ADBMS_LINK_IC_COUNT 5U'),
 ('lib/ams_core/adbms/ams_adbms_link.c','?0x2cU:0x02U','?0x27U:0x01U'),
 ('lib/ams_core/adbms/ams_adbms_link.c','pec!=ams_link_pec10(rx,counter)','false'),
 ('lib/ams_core/adbms/ams_adbms_link.c','if (mismatch) return AMS_LINK_COUNTER;','/* removed */'),
 ('lib/ams_core/adbms/ams_adbms_link.c','if (guarded) return AMS_LINK_SESSION_EXPIRED;','/* removed */'),
 ('drivers/ams/adbms_link_probe.c','if (result!=AMS_LINK_OK) step=4U;','/* keep retrying */'),
 ('drivers/ams/adbms_spi_stm32.c','string != AMS_ADBMS_SPI_STRING_B','false'),
 ('drivers/ams/adbms_spi_stm32.c','link_owner==k_current_get()','true'),
 ('drivers/ams/adbms_time_zephyr.c','i<budget','true'),
 ('app/z016_isospi_probe.conf','CONFIG_AMS_Z016_LINK_PROBE=y','CONFIG_AMS_Z016_LINK_PROBE=y\nCONFIG_AMS_BALANCE_AUTHORITY=y'),
]
with tempfile.TemporaryDirectory(prefix='z016-mutations-') as d:
 root=Path(d)/'repo'
 shutil.copytree(repo,root,ignore=shutil.ignore_patterns('build','__pycache__','.git'))
 command=[sys.executable,str(root/'scripts/check_z016_link_contract.py'),str(root)]
 subprocess.run(command,check=True)
 for file,old,new in cases:
  p=root/file;original=p.read_text()
  assert old in original,(file,old)
  p.write_text(original.replace(old,new,1))
  run=subprocess.run(command,capture_output=True,text=True)
  p.write_text(original)
  if run.returncode==0: raise SystemExit('FAIL: mutation survived: '+old)
print(f'PASS: {len(cases)} Z016 unsafe profile/protocol/ownership/timing changes rejected')
