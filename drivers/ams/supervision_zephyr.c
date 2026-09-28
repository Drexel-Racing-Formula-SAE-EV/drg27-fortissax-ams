#include <ams_private/supervision_owner.h>
#include <ams_platform/supervision.h>
#include <zephyr/spinlock.h>
#include <string.h>
static struct k_spinlock lock;
static k_tid_t owners[AMS_SUP_ACTORS],supervisor;
static bool bound,pending,temp_complete;
static uint32_t pending_faults;
static ams_supervision_scan_t scan;
static ams_supervision_record_t records[AMS_SUP_ACTORS];
static ams_supervision_t decision;
static ams_segment_fault_t voltage,temperature;
static ams_segment_fault_t published_voltage,published_temperature;
static uint32_t published_converted,published_failed,published_suppressed,published_generation;
static uint32_t voltage_data_ms,temperature_data_ms,published_voltage_ms,published_temperature_ms;
bool ams_z023_bind(k_tid_t current,k_tid_t adbms,k_tid_t safety)
{
 if(k_is_in_isr() || !current || !adbms || !safety || current==adbms ||
    current==safety || adbms==safety)return false;
 k_spinlock_key_t key=k_spin_lock(&lock);
 bool ok=!bound;
 if(ok){owners[0]=current;owners[1]=owners[2]=adbms;supervisor=safety;
  ams_supervision_init(&decision,k_uptime_get_32());bound=true;}
 k_spin_unlock(&lock,key);return ok;
}
bool ams_z023_owner(unsigned actor)
{ return bound && !k_is_in_isr() && actor<AMS_SUP_ACTORS && k_current_get()==owners[actor]; }
static void complete(unsigned actor,uint32_t faults)
{
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!ams_supervision_next(&records[actor],k_uptime_get_32(),faults))
  decision.sticky_faults|=AMS_SUP_PROTOCOL;
 k_spin_unlock(&lock,key);
}
void ams_z023_current_complete(uint32_t faults)
{ if(ams_z023_owner(AMS_SUP_CURRENT))complete(AMS_SUP_CURRENT,faults); }
void ams_z023_temperature_result(uint32_t generation,uint8_t position,
 uint32_t converted,uint32_t failed,uint32_t suppressed)
{
 if(!ams_z023_owner(AMS_SUP_TEMP))return;
 temp_complete=ams_supervision_scan(&scan,generation,position,converted,failed,suppressed);
}
void ams_z023_monitor_result(const ams_adbms_monitor_platform_snapshot_t *m)
{
 if(!m || !ams_z023_owner(AMS_SUP_ADBMS))return;
 pending=true;pending_faults=0;
 if(!m->initialized || !m->config_verified || !m->balance_mute_verified ||
    !m->balance_durable_zero_verified || m->recovery.continuity_lost)pending_faults|=AMS_SUP_CONFIG;
 if(m->snapshot_cleanup_required || m->temperature.config_cleanup_required)pending_faults|=AMS_SUP_CLEANUP;
 if(m->recovery.terminal)pending_faults|=AMS_SUP_TERMINAL;
 if(m->last_result || m->sticky_diag_faults)pending_faults|=AMS_SUP_PROTOCOL;
 if(m->cells.usable_mask!=0x7fffU || m->temperature.image.usable_mask!=0xffffffU)
  pending_faults|=AMS_SUP_INVALID;
 uint32_t now=k_uptime_get_32();
 voltage_data_ms=now;temperature_data_ms=now;
 for(unsigned i=0;i<15U;i++)if((uint32_t)(now-m->cells.last_update_ms[i])>(uint32_t)(now-voltage_data_ms))
  voltage_data_ms=m->cells.last_update_ms[i];
 for(unsigned i=0;i<24U;i++)if((uint32_t)(now-m->temperature.image.last_update_ms[i])>(uint32_t)(now-temperature_data_ms))
  temperature_data_ms=m->temperature.image.last_update_ms[i];
 for(unsigned i=0;i<15U;i++)if((uint32_t)(now-m->cells.last_update_ms[i])>2500U)
  pending_faults|=AMS_SUP_DATA_STALE;
 for(unsigned i=0;i<24U;i++)if((uint32_t)(now-m->temperature.image.last_update_ms[i])>12000U)
  pending_faults|=AMS_SUP_DATA_STALE;
 bool coherent=m->initialized && m->config_verified && m->acquisition_live &&
  m->state==AMS_ADBMS_MONITOR_PLATFORM_READY && !m->recovery.pending &&
  !m->recovery.terminal && !m->recovery.continuity_lost &&
  !m->snapshot_cleanup_required && !m->temperature.config_cleanup_required;
 ams_segment_voltage_fault(&voltage,&m->cells,coherent,m->statd_valid,
   m->statd.cell_ov_mask,m->statd.cell_uv_mask,now);
 ams_segment_temperature_fault(&temperature,&m->temperature.image,coherent,now);
 if(voltage.confirmed || voltage.latched || temperature.confirmed || temperature.latched)
  pending_faults|=AMS_SUP_PROCESS;
 if(m->recovery.pending || m->recovery.terminal || m->recovery.continuity_lost) {
  memset(&scan,0,sizeof(scan));temp_complete=false;
 }
}
void ams_z023_publication_complete(bool published)
{
 if(!ams_z023_owner(AMS_SUP_ADBMS) || !pending)return;
 pending=false;
 if(!published)pending_faults|=AMS_SUP_PUBLICATION;
 k_spinlock_key_t key=k_spin_lock(&lock);
 uint32_t now=k_uptime_get_32();
 published_voltage=voltage;published_temperature=temperature;
 published_voltage_ms=voltage_data_ms;published_temperature_ms=temperature_data_ms;
 if(!ams_supervision_next(&records[AMS_SUP_ADBMS],now,pending_faults))decision.sticky_faults|=AMS_SUP_PROTOCOL;
 if(temp_complete){
  published_converted=scan.converted;published_failed=scan.failed;
  published_suppressed=scan.suppressed;published_generation=scan.generation;
  uint32_t faults=(scan.failed || scan.suppressed || !temperature.valid ? AMS_SUP_INVALID:0U)|
    (temperature.confirmed || temperature.latched ? AMS_SUP_PROCESS:0U)|(published?0U:AMS_SUP_PUBLICATION);
  if(!ams_supervision_next(&records[AMS_SUP_TEMP],now,faults))decision.sticky_faults|=AMS_SUP_PROTOCOL;
 }
 k_spin_unlock(&lock,key);
 if(temp_complete){memset(&scan,0,sizeof(scan));temp_complete=false;}
}
bool ams_z023_poll(ams_supervision_t *out)
{
 if(!bound || !out || k_is_in_isr() || k_current_get()!=supervisor)return false;
 k_spinlock_key_t key=k_spin_lock(&lock);
 uint32_t now=k_uptime_get_32();
 for(unsigned a=0;a<AMS_SUP_ACTORS;a++) {
  /* Retain faults even if delayed handoff is too old to earn progress. */
  decision.sticky_faults|=records[a].history;
  if(ams_supervision_commit(&decision,a,&records[a],now)) {
   if(a==AMS_SUP_ADBMS) {
    decision.voltage=published_voltage;decision.temperature=published_temperature;
    decision.voltage_data_ms=published_voltage_ms;decision.temperature_data_ms=published_temperature_ms;
   }
   if(a==AMS_SUP_TEMP) {
    decision.temp_converted=published_converted;decision.temp_failed=published_failed;
    decision.temp_suppressed=published_suppressed;decision.temp_generation=published_generation;
   }
  }
 }
 ams_supervision_evaluate(&decision,now);*out=decision;
 k_spin_unlock(&lock,key);return true;
}
bool ams_z023_copy(ams_supervision_t *out)
{
 if(!bound || !out || k_is_in_isr())return false;
 k_spinlock_key_t key=k_spin_lock(&lock);*out=decision;k_spin_unlock(&lock,key);return true;
}
