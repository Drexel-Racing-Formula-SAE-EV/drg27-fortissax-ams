#!/usr/bin/env python3
import shutil, subprocess, sys, tempfile
from pathlib import Path
repo=Path(sys.argv[1]).resolve()
cases=[
 ('lib/ams_core/include/ams_core/ams_adbms_link.h','AMS_ADBMS_LINK_STRING 1U','AMS_ADBMS_LINK_STRING 0U'),
 ('lib/ams_core/include/ams_core/ams_adbms_link.h','AMS_ADBMS_LINK_IC_COUNT 1U','AMS_ADBMS_LINK_IC_COUNT 5U'),
 ('lib/ams_core/include/ams_core/ams_adbms_link.h','typedef enum { AMS_LINK_SID, AMS_LINK_CFGA } ams_link_read_t;','typedef enum { AMS_LINK_SID, AMS_LINK_CFGA, AMS_LINK_WRITE } ams_link_read_t;'),
 ('lib/ams_core/adbms/ams_adbms_link.c','AMS_ADBMS_CMD_RDSID : AMS_ADBMS_CMD_RDCFGA','AMS_ADBMS_CMD_SRST : AMS_ADBMS_CMD_WRCFGA'),
 ('lib/ams_core/adbms/ams_adbms_link.c','ams_adbms_decode_packet(rx, &link->counter, &packet)','AMS_ADBMS_RESULT_OK'),
 ('lib/ams_core/adbms/ams_adbms_link.c','if (guarded) {','if (false) {'),
 ('drivers/ams/adbms_link_probe.c','if (result!=AMS_LINK_OK) step=4U;','/* keep retrying */'),
 ('drivers/ams/adbms_spi_stm32.c','string != AMS_ADBMS_SPI_STRING_B','false'),
 ('drivers/ams/adbms_spi_stm32.c','link_owner==k_current_get()','true'),
 ('drivers/ams/adbms_time_zephyr.c','i<budget','true'),
 ('app/Kconfig','depends on !AMS_Z017_CELL_VALIDATION','depends on AMS_Z017_CELL_VALIDATION'),
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
  if run.returncode==0: raise SystemExit('FAIL: Z016 mutation survived: '+old)
print(f'PASS: {len(cases)} Z016 unsafe profile/protocol/ownership/timing changes rejected')
