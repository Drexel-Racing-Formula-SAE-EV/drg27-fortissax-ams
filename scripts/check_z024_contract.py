#!/usr/bin/env python3
"""Z024 portable checkpoint only: not a target/ELF qualification gate."""
from pathlib import Path
import hashlib,re,sys
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
o=r/'tests/unit/z024/oracle'
a=(r/'lib/ams_core/can/ams_can_tx_scheduler.c').read_text()
b=(o/'can_tx_scheduler.c').read_text()
assert hashlib.sha256((o/'can_tx_scheduler.c').read_bytes()).hexdigest()=='9fe7b7ad86bac0c888e2e58b19b5686cf0e25e1cb7ce9347b15ed4ca7f291503'
assert a==b.replace('"ext_drivers/can_tx_scheduler.h"','<ams_core/ams_can_tx_scheduler.h>'), 'scheduler oracle drift'
assert (r/'lib/ams_core/include/ams_core/ams_can_tx_scheduler.h').read_bytes()==(o/'can_tx_scheduler.h').read_bytes()
for name in ['CAN_ADAPTER_PRESENT','CAN_ACTOR_LIVE','CAN_SAFETY_EVIDENCE']:
 k=(r/'app/Kconfig').read_text();block=k.split('config AMS_CAP_'+name+'\n')[1].split('\nconfig ')[0]
 assert 'default n' in block and 'default y' not in block,name
assert 'can/ams_can_transport.c' in (r/'lib/ams_core/CMakeLists.txt').read_text()
assert 'can/ams_can_codec.c' in (r/'lib/ams_core/CMakeLists.txt').read_text()
print('PASS Z024 portable contract; runtime CAN remains disabled; target mode not implemented')
