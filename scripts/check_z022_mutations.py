#!/usr/bin/env python3
from pathlib import Path
import contextlib,io,shutil,sys,tempfile
from check_z022_contract import source_check
r=Path(sys.argv[1]).resolve();c='lib/ams_core/measurement/ams_measurement_pipeline.c';a='drivers/ams/measurement_pipeline_zephyr.c'
cases=[(c,x,y) for x,y in [('atomic_store(&p->dropped_current,true)','atomic_store(&p->dropped_current,false)'),('atomic_exchange(&p->dropped_current,false)','false'),('s->cell_age_ms[seg][i]=UINT32_MAX','s->cell_age_ms[seg][i]=0'),('if(c->iir_ready)','if(true)'),('if (coherent && c && t)','if (c && t)'),('p->boundary_ready=false;','/* retained boundary */')]]
cases += [(a,x,'true') for x in ['k_current_get()!=current_owner','k_current_get()!=voltage_owner','!m.recovery.pending','!m.recovery.terminal','!m.recovery.continuity_lost','!m.snapshot_cleanup_required','!m.temperature.config_cleanup_required']]
cases += [('app/z022_measurement_validation.conf','CONFIG_'+x+'=n','CONFIG_'+x+'=y') for x in ['AMS_BMS_AUTHORITY','AMS_BALANCE_AUTHORITY']]
cases += [('lib/ams_core/estimator/ams_segment_consumer.c',x,'false') for x in ['s->sequence==c->sequence','(uint32_t)(now-s->publication_tick)>100U','s->current.total_invalid_sample_count==c->invalid_total','s->cell_usable_mask[seg]==AMS_CELL_IMAGE_MONITORED_MASK']]
source_check(r)
with tempfile.TemporaryDirectory(prefix='z022-mutations-') as td:
 d=Path(td)
 for top in ['app','lib','drivers']:shutil.copytree(r/top,d/top)
 for f,old,new in cases:
  p=d/f;s=p.read_text();assert old in s;p.write_text(s.replace(old,new));rejected=False
  try:
   with contextlib.redirect_stdout(io.StringIO()):
    try:source_check(d)
    except SystemExit as e:rejected=e.code not in (0,None)
  finally:p.write_text(s)
  if not rejected:raise SystemExit('FAIL surviving mutation '+old)
print(f'PASS Z022 mutations: {len(cases)}/{len(cases)} rejected')
