#!/usr/bin/env python3
"""Source and optional generated-profile gate for the restricted Z016 link."""
import re
import sys
from pathlib import Path

def check(repo, build=None):
 def require(ok, msg):
  if not ok: raise SystemExit("FAIL: Z016 " + msg)
 core=(repo/"lib/ams_core/adbms/ams_adbms_link.c").read_text()
 header=(repo/"lib/ams_core/include/ams_core/ams_adbms_link.h").read_text()
 probe=(repo/"drivers/ams/adbms_link_probe.c").read_text()
 target=(repo/"drivers/ams/adbms_spi_stm32.c").read_text()
 timing=(repo/"drivers/ams/adbms_time_zephyr.c").read_text()
 for token in ("AMS_ADBMS_LINK_STRING 1U", "AMS_ADBMS_LINK_IC_COUNT 1U", "AMS_ADBMS_LINK_GUARD_US 3000U"):
  require(token in header,"profile changed: " + token)
 require('cmd[1]=(kind==AMS_LINK_SID)?0x2cU:0x02U;' in core,"read command allowlist drift")
 require('pec!=ams_link_pec10(rx,counter)' in core,"PEC validation removed")
 require('if (mismatch) return AMS_LINK_COUNTER;' in core,"counter validation removed")
 require('if (guarded) return AMS_LINK_SESSION_EXPIRED;' in core,"coherent expiry softened")
 require('AMS_ADBMS_SPI_STRING_B,cmd,4U,rx,8U' in probe,"physical link/count changed")
 require('if (result!=AMS_LINK_OK) step=4U;' in probe,"probe retries after failure")
 require('string != AMS_ADBMS_SPI_STRING_B' in target,"String A exclusion removed")
 require('link_owner==k_current_get()' in target,"owner identity missing")
 require('if (!link_owner_valid()) return false;' in target,"wake ownership missing")
 require('for (uint32_t i=0; i<budget; ++i)' in timing,"timing bound removed")
 require('k_cycle_get_64()' in timing,"64-bit clock removed")
 for name in ('app/prj.conf','app/z016_isospi_probe.conf'):
  text=(repo/name).read_text()
  for flag in ('AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY','AMS_ADBMS_SPI_PHYSICAL_VALIDATED'):
   require('CONFIG_'+flag+'=y' not in text,"unqualified authority/physical claim")
 for path in (repo/'app').rglob('*.c'):
  if path.name != 'ams_threads.c':
   require('ams_adbms_link_probe_step(' not in path.read_text(),"probe called outside owner thread")
 require('if (thread == &threads[AMS_THREAD_ADBMS])' in (repo/'app/src/ams_threads.c').read_text(),
         "probe not restricted to ADBMS descriptor")
 if build is not None:
  cfg=(build/'zephyr/.config').read_text().splitlines()
  if 'CONFIG_AMS_Z016_LINK_PROBE=y' in cfg:
   for flag in ('CORTEX_M_SYSTICK_64BIT_CYCLE_COUNTER','TIMER_HAS_64BIT_CYCLE_COUNTER'):
    require('CONFIG_'+flag+'=y' in cfg,"missing timing option " + flag)
   require('CONFIG_PM=y' not in cfg,"unqualified power management")
 print('PASS: Z016 String B/one-SMB read-only profile, ownership, integrity and timing contract')

if __name__=='__main__':
 check(Path(sys.argv[1]),Path(sys.argv[2]) if len(sys.argv)>2 else None)
