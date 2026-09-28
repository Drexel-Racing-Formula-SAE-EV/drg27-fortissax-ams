#include <ams_core/ams_measurement_pipeline.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static ams_measurement_pipeline_t p;
static bool held,fail,inject;
static uint32_t tick;
static bool lock(void *c){(void)c;if(fail)return false;assert(!held);held=true;
 if(inject){inject=false;tick=110;ams_current_window_set_sensor_metadata(&p.current,100,1);ams_current_window_update(&p.current,tick,10,10,true,true,7);}return true;}
static void unlock(void *c){(void)c;assert(held);held=false;}
static uint32_t now(void *c){(void)c;return tick;}
static uintptr_t meta(void *c){(void)c;return 0;}
static void unmeta(void *c,uintptr_t key){(void)c;(void)key;}
int main(void){
 ams_pipeline_clock_lock_t ops={NULL,lock,unlock,now};ams_measurement_lock_ops_t m={NULL,meta,unmeta};
 tick=90;assert(ams_pipeline_init(&p,&ops,&m));
 assert(ams_pipeline_current(&p,10,10,true,true,7,100,1));
 tick=100;inject=true;assert(ams_pipeline_boundary(&p));
 assert(p.boundary==110&&p.completed.end_tick==110&&p.completed.valid);
 assert(fabs(p.completed.charge_As-0.2)<1e-6);
 ams_cell_image_t cells={0};ams_temp_image_t temps={0};
 cells.usable_mask=0x7fff;cells.iir_usable_mask=0x7fff;temps.usable_mask=temps.valid_mask=0xffffff;
 for(unsigned i=0;i<15;i++){cells.raw_mv[i]=4000;cells.raw_valid[i]=true;cells.last_update_ms[i]=110;}
 for(unsigned i=0;i<24;i++){temps.deci_c[i]=250;temps.last_update_ms[i]=110;}
 assert(ams_pipeline_publish_single(&p,&cells,&temps,true,90)==1);
 assert(!ams_pipeline_publish_single(&p,&cells,&temps,true,90));
 ams_measurement_snapshot_t s;assert(ams_measurement_store_copy_latest(&p.store,&s));
 assert(s.cell_usable_mask[0]==0x7fff&&s.temp_usable_mask[0]==0xffffff&&!s.cell_iir_usable_mask[0]);
 for(unsigned seg=1;seg<5;seg++)assert(!s.cell_usable_mask[seg]&&!s.temp_usable_mask[seg]&&s.cell_age_ms[seg][0]==UINT32_MAX);
 assert(!(s.validity_flags&(AMS_MEAS_VALID_VOLTAGE|AMS_MEAS_VALID_TEMPERATURE)));
 assert(!ams_pipeline_estimator_eligible(&s,110));
 tick=120;assert(ams_pipeline_boundary(&p));assert(ams_pipeline_publish_single(&p,&cells,&temps,false,120));
 assert(ams_measurement_store_copy_latest(&p.store,&s));assert(!s.cell_usable_mask[0]&&!s.temp_usable_mask[0]);
 tick=130;assert(ams_pipeline_boundary(&p));unsigned inactive=1U-p.store.published_index;p.store.reader_count[inactive]=1;
 assert(!ams_pipeline_publish_single(&p,&cells,&temps,true,130)&&!p.boundary_ready);p.store.reader_count[inactive]=0;
 assert(p.store.publication_drop_count==1);
 fail=true;assert(!ams_pipeline_current(&p,10,10,true,true,7,100,1));fail=false;tick=140;assert(ams_pipeline_boundary(&p));assert(!p.completed.valid);
 fail=true;assert(!ams_pipeline_boundary(&p)&&!p.boundary_ready);fail=false;
 tick=UINT32_MAX-10;assert(ams_pipeline_init(&p,&ops,&m));assert(ams_pipeline_current(&p,10,10,true,true,7,100,1));tick=9;assert(ams_pipeline_boundary(&p)&&p.completed.valid);
 tick=13000;assert(ams_pipeline_boundary(&p));assert(ams_pipeline_publish_single(&p,&cells,&temps,true,12900));assert(ams_measurement_store_copy_latest(&p.store,&s));assert(!s.cell_usable_mask[0]&&!s.temp_usable_mask[0]);
 /* A complete synthetic fixture tests admission only, never publication. */
 memset(&s,0,sizeof(s));s.sequence=1;s.publication_tick=100;s.voltage_complete_tick=100;s.current.end_tick=100;s.current.latest_sample_tick=100;
 s.validity_flags=AMS_MEAS_VALID_CURRENT|AMS_MEAS_VALID_VOLTAGE|AMS_MEAS_VALID_TEMPERATURE;
 s.current.valid=true;s.current.calibration_record_confident=true;s.current.calibration_id=7;s.current.uncertainty_mA=100;
 for(unsigned seg=0;seg<5;seg++){s.cell_usable_mask[seg]=0x7fff;s.temp_usable_mask[seg]=0xffffff;for(unsigned i=0;i<15;i++)s.cell_mv[seg][i]=4000;}
 assert(ams_pipeline_estimator_eligible(&s,100));s.current.calibration_record_confident=false;assert(!ams_pipeline_estimator_eligible(&s,100));s.current.calibration_record_confident=true;
 s.cell_usable_mask[4]=0;assert(!ams_pipeline_estimator_eligible(&s,100));s.cell_usable_mask[4]=0x7fff;
 s.current.end_tick=101;assert(!ams_pipeline_estimator_eligible(&s,100));s.current.end_tick=100;
 assert(!ams_pipeline_estimator_eligible(&s,201));s.current.uncertainty_mA=UINT16_MAX;assert(!ams_pipeline_estimator_eligible(&s,100));
 puts("PASS Z022 pipeline: crossing-current race, lock failure taint, immutable/drop publication, raw authority, absent segments, age/wrap and estimator admission");
}
