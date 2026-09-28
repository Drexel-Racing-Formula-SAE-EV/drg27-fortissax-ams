#include <ams_platform/measurement_pipeline.h>
#include <ams_platform/current_adc.h>
#include <ams_platform/adbms_monitor.h>
#include <ams_core/ams_measurement_pipeline.h>
#include <ams_core/ams_current_sensor.h>
#include <ams_core/ams_segment_consumer.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
#include <ams_platform/supervision.h>
#endif

static ams_measurement_pipeline_t pipeline;
static current_sensor_t sensor;
static ams_segment_consumer_t consumer;
static current_fault_state_t current_fault;
static ams_z022_diagnostics_t diagnostics;
static struct k_spinlock diagnostics_lock;
static k_tid_t estimator_owner;
K_MUTEX_DEFINE(current_lock);
static struct k_spinlock metadata_lock;
static k_tid_t current_owner, voltage_owner;
static bool ready, attempted, boundary_attempted;
static bool take(void *context) { (void)context;return k_mutex_lock(&current_lock,K_MSEC(2))==0; }
static void give(void *context) { (void)context;k_mutex_unlock(&current_lock); }
static uint32_t now(void *context) { (void)context;return k_uptime_get_32(); }
static ams_measurement_lock_key_t meta_enter(void *context)
{ (void)context;return (ams_measurement_lock_key_t)k_spin_lock(&metadata_lock).key; }
static void meta_exit(void *context,ams_measurement_lock_key_t key)
{ (void)context;k_spin_unlock(&metadata_lock,(k_spinlock_key_t){.key=(unsigned int)key}); }
bool ams_z022_init(void)
{
 if(attempted||k_is_in_isr())return false;
 attempted=true;current_sensor_init(&sensor);current_fault_init(&current_fault);ams_segment_consumer_init(&consumer);
 const ams_pipeline_clock_lock_t clock={NULL,take,give,now};
 const ams_measurement_lock_ops_t metadata={NULL,meta_enter,meta_exit};
 ready=ams_pipeline_init(&pipeline,&clock,&metadata);return ready;
}
void ams_z022_current_step(void)
{
 if(!ready||k_is_in_isr())return;
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
 if(!ams_z023_owner(AMS_SUP_CURRENT))return;
#endif
 if(!current_owner)current_owner=k_current_get();
 if(k_current_get()!=current_owner)return;
 ams_current_adc_pair_t pair={0};current_sensor_adc_begin(&sensor);
 int result=ams_current_adc_read_pair(&pair);
 if(result==0&&pair.complete&&!pair.adapter_faulted) {
  if(pair.high_fresh)current_sensor_adc_publish_high(&sensor,pair.high_count);
  if(pair.low_fresh)current_sensor_adc_publish_low(&sensor,pair.low_count);
 }
 (void)current_sensor_adc_finish(&sensor);(void)current_sensor_convert(&sensor);
 uint16_t uncertainty=AMS_CURRENT_UNCERTAINTY_UNKNOWN;
 if(current_sensor_calibration_confident(&sensor)) {
  if(sensor.selected_range==CURRENT_SENSOR_RANGE_50A)uncertainty=sensor.calibration_uncertainty_50a_mA;
  else if(sensor.selected_range==CURRENT_SENSOR_RANGE_800A)uncertainty=sensor.calibration_uncertainty_800a_mA;
 }
 /* The no-authority bench has no charge/drive state machine. Preserve the
  * oracle STATE_START conservative precharge policy; do not infer drive mode. */
 current_fault_update(&current_fault,CURRENT_FAULT_MODE_PRECHARGE,sensor.current,
    sensor.current_valid,sensor.reason,20U);
 bool accepted=ams_pipeline_current(&pipeline,sensor.current,sensor.current_filtered,sensor.current_valid,
    current_sensor_calibration_confident(&sensor),sensor.calibration_id,uncertainty,(uint8_t)sensor.selected_range);
 k_spinlock_key_t key=k_spin_lock(&diagnostics_lock);
 diagnostics.current_fault=current_fault;diagnostics.current_window_accepted=accepted;
 diagnostics.current_completed_ms=k_uptime_get_32();
 if(diagnostics.current_completions!=UINT32_MAX)diagnostics.current_completions++;
 k_spin_unlock(&diagnostics_lock,key);
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
 uint32_t faults=sensor.current_valid?0U:AMS_SUP_INVALID;
 if(current_fault.sensor_fault || current_fault.pending || current_fault.confirmed || current_fault.latched)faults|=AMS_SUP_PROCESS;
 if(!accepted)faults|=AMS_SUP_PUBLICATION;
 if(!current_sensor_calibration_confident(&sensor) || uncertainty==AMS_CURRENT_UNCERTAINTY_UNKNOWN || !uncertainty)faults|=AMS_SUP_UNQUALIFIED;
 ams_z023_current_complete(faults);
#endif
}
void ams_z022_begin_release(void)
{
 if(!ready||k_is_in_isr())return;
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
 if(!ams_z023_owner(AMS_SUP_ADBMS))return;
#endif
 if(!voltage_owner)voltage_owner=k_current_get();
 if(k_current_get()!=voltage_owner)return;
 boundary_attempted=false;pipeline.boundary_ready=false;
}
void ams_z022_voltage_boundary(void)
{
 if(!ready||k_is_in_isr()||k_current_get()!=voltage_owner||boundary_attempted)return;
 boundary_attempted=true;(void)ams_pipeline_boundary(&pipeline);
}
void ams_z022_publish_release(void)
{
 if(!ready||k_is_in_isr()||k_current_get()!=voltage_owner)return;
 ams_z022_voltage_boundary();
 pipeline.balance_zero_verified=false;
 ams_adbms_monitor_platform_snapshot_t m;
 if(!ams_adbms_monitor_platform_snapshot(&m)) {
  (void)ams_pipeline_publish_single(&pipeline,NULL,NULL,false,pipeline.boundary);
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
  ams_z023_publication_complete(false);
#endif
  return;
 }
 bool coherent=m.initialized&&m.config_verified&&m.acquisition_live&&
   m.state==AMS_ADBMS_MONITOR_PLATFORM_READY&&!m.recovery.pending&&!m.recovery.terminal&&
   !m.recovery.continuity_lost&&!m.snapshot_cleanup_required&&!m.temperature.config_cleanup_required;
 pipeline.balance_zero_verified=coherent&&m.balance_mute_verified&&m.balance_durable_zero_verified;
 ams_sequence_t published=ams_pipeline_publish_single(&pipeline,&m.cells,&m.temperature.image,coherent,m.attempted_ms);
#ifdef CONFIG_AMS_Z023_SUPERVISION_VALIDATION
 ams_z023_publication_complete(published!=0U);
#else
 (void)published;
#endif
}
bool ams_z022_copy_latest(ams_measurement_snapshot_t *out)
{ return ready&&!k_is_in_isr()&&ams_measurement_store_copy_latest(&pipeline.store,out); }

void ams_z022_estimator_step(void)
{
 if(!ready||k_is_in_isr())return;
 if(!estimator_owner)estimator_owner=k_current_get();
 if(k_current_get()!=estimator_owner)return;
 ams_measurement_snapshot_t s;
 bool have=ams_z022_copy_latest(&s);
 (void)ams_segment_consumer_step(&consumer,have?&s:NULL,k_uptime_get_32());
 k_spinlock_key_t key=k_spin_lock(&diagnostics_lock);
 diagnostics.estimator_epochs=consumer.accepted_epochs;
 diagnostics.estimator_current_trusted=consumer.current_trusted;
 diagnostics.estimator_valid_mask=0;
 for(unsigned i=0;i<AMS_PHYSICAL_SEGMENT_COUNT;i++) {
  diagnostics.segment_soc[i]=consumer.segment[i].soc;
  diagnostics.segment_faults[i]=consumer.segment[i].fault_flags;
  if(consumer.segment[i].valid)diagnostics.estimator_valid_mask|=(uint8_t)(1U<<i);
 }
 k_spin_unlock(&diagnostics_lock,key);
}
bool ams_z022_copy_diagnostics(ams_z022_diagnostics_t *out)
{
 if(!ready||!out||k_is_in_isr())return false;
 k_spinlock_key_t key=k_spin_lock(&diagnostics_lock);*out=diagnostics;k_spin_unlock(&diagnostics_lock,key);return true;
}
