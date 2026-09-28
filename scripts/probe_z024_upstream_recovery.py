#!/usr/bin/env python3
"""Reproduce pinned upstream return-code hazard; this is not adapter validation."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,sys
r=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
d=r/'docs/migration/oracle/z024/zephyr-v4.4.0'
p=d/'can_stm32_bxcan.c';manifest=json.loads((d/'SOURCE_MANIFEST.json').read_text())
assert hashlib.sha256(p.read_bytes()).hexdigest()==manifest['files']['drivers/can/can_stm32_bxcan.c']
s=p.read_text();a=s.index('static int can_stm32_recover(');b=s.index('\n}\n',a)+3
fn=s[a:b]
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stddef.h>
typedef struct { int64_t ticks; } k_timeout_t;
#define K_FOREVER ((k_timeout_t){-1})
#define K_TIMEOUT_EQ(a,b) ((a).ticks==(b).ticks)
#define CAN_ESR_BOFF 1U
#define CAN_MODE_MANUAL_RECOVERY 1U
typedef struct {uint32_t ESR;} CAN_TypeDef;
struct can_stm32_config {CAN_TypeDef *can;};
struct can_stm32_data {struct {bool started;unsigned mode;} common;int inst_mutex;};
struct device {void *data;const void *config;};
static int leave_result;
static int k_mutex_lock(int *p,k_timeout_t t){(void)p;(void)t;return 0;}
static void k_mutex_unlock(int *p){(void)p;}
static int64_t k_uptime_ticks(void){return 0;}
static int can_stm32_enter_init_mode(CAN_TypeDef *p){(void)p;return 0;}
static int can_stm32_leave_init_mode(CAN_TypeDef *p){(void)p;return leave_result;}
'''
post=r'''
int main(void){
 CAN_TypeDef can={.ESR=CAN_ESR_BOFF};struct can_stm32_config cfg={.can=&can};
 struct can_stm32_data data={.common={true,CAN_MODE_MANUAL_RECOVERY}};
 struct device dev={.data=&data,.config=&cfg};
 assert(can_stm32_recover(&dev,(k_timeout_t){0})==0);
 assert(can.ESR&CAN_ESR_BOFF);
 leave_result=-EIO;
 assert(can_stm32_recover(&dev,(k_timeout_t){0})==0);
 assert(can.ESR&CAN_ESR_BOFF);
}
'''
with tempfile.TemporaryDirectory(prefix='z024-upstream-') as td:
 td=Path(td);f=td/'probe.c';f.write_text(pre+fn+post)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(td/'probe')],check=True)
 subprocess.run([str(td/'probe')],check=True)
print('REPRODUCED upstream 4.4.0 hazard: recovery returns 0 while bus-off persists, including leave-init failure')
