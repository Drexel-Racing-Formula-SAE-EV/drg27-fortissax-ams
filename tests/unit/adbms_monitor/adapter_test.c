#include <ams_platform/adbms_monitor.h>
#include <ams_platform/adbms_spi_lifecycle.h>
#include <ams_core/ams_adbms_monitor.h>
#include "adbms_spi_internal.h"
#include "fake_zephyr_spi.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

uintptr_t fake_current_thread = 1U;
bool fake_isr;
bool fake_clock_stalled;
uint64_t fake_cycles;
#ifdef CONFIG_AMS_Z022_MEASUREMENT_VALIDATION
static unsigned boundary_calls;
void ams_z022_voltage_boundary(void) { ++boundary_calls; }
#endif
static unsigned recovery_calls, interrupt_calls;
static bool recovery_pending_stub, recovery_fail_stub, cell_fail_stub;
static unsigned temp_calls, aux2_calls, ow_calls;
static unsigned binds, wakes, writes, reads, init_calls, acquire_calls;
static ams_adbms_spi_result_t wake_result = AMS_ADBMS_SPI_RESULT_OK;
static ams_adbms_spi_result_t transfer_result = AMS_ADBMS_SPI_RESULT_OK;
static ams_adbms_spi_platform_state_t platform_state = AMS_ADBMS_SPI_PLATFORM_READY;
static ams_adbms_monitor_io_t captured_io;

bool ams_adbms_spi_bind_owner(void) { binds++; return binds == 1U; }
ams_adbms_spi_result_t ams_adbms_spi_wake_b(bool cold)
{ (void)cold; wakes++; return wake_result; }
ams_adbms_spi_result_t ams_adbms_spi_write(ams_adbms_spi_string_t string,
 const uint8_t *tx,size_t len)
{ assert(string==AMS_ADBMS_SPI_STRING_B && tx!=NULL && len!=0U); writes++; return transfer_result; }
ams_adbms_spi_result_t ams_adbms_spi_write_read(ams_adbms_spi_string_t string,
 const uint8_t *tx,size_t tx_len,uint8_t *rx,size_t rx_len)
{ assert(string==AMS_ADBMS_SPI_STRING_B && tx!=NULL && tx_len!=0U && rx!=NULL && rx_len!=0U); reads++; memset(rx,0,rx_len); return transfer_result; }
ams_adbms_spi_platform_status_t ams_adbms_spi_platform_status(void)
{ ams_adbms_spi_platform_status_t s={.state=platform_state}; return s; }

void ams_adbms_monitor_reset(ams_adbms_monitor_t *m)
{ memset(m,0,sizeof(*m)); m->state=AMS_ADBMS_MONITOR_UNINITIALIZED; }
ams_adbms_result_t ams_adbms_monitor_initialize(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io)
{
 uint8_t tx=0x5a,rx=0; init_calls++; captured_io=*io;
 assert(io->now_us && io->delay_us && io->wake_b && io->write_b && io->write_read_b);
 assert(io->wake_b(io->context,false)==AMS_ADBMS_RESULT_OK);
 assert(io->delay_us(io->context,3000U)==AMS_ADBMS_RESULT_OK);
 assert(io->write_b(io->context,&tx,1U)==AMS_ADBMS_RESULT_OK);
 assert(io->write_read_b(io->context,&tx,1U,&rx,1U)==AMS_ADBMS_RESULT_OK);
 m->state=AMS_ADBMS_MONITOR_READY; m->initialized=true; m->config_verified=true;
 m->startup_post_passed=true; m->acquisition_live=true; m->last_result=AMS_ADBMS_RESULT_OK;
 m->balance_inhibit_attempt_count=1U; m->balance_mute_verified=true;
 m->balance_durable_zero_verified=true;
 return AMS_ADBMS_RESULT_OK;
}
ams_adbms_result_t ams_adbms_monitor_acquire(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint32_t now_ms)
{
 if(cell_fail_stub) return AMS_ADBMS_RESULT_TRANSPORT_IO;
 uint8_t tx=0xa5; acquire_calls++; assert(io==&captured_io || io->write_b==captured_io.write_b);
 assert(io->delay_us(io->context,10U)==AMS_ADBMS_RESULT_OK);
 assert(io->write_b(io->context,&tx,1U)==AMS_ADBMS_RESULT_OK);
 m->scan_attempt_count++;m->scan_success_count++;m->attempted_ms=now_ms;m->successful_ms=now_ms;
 m->cells.usable_mask=0x7fffU;m->cells.usable_count=15U;
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
 for(unsigned i=0;i<15;i++){m->cells.raw_valid[i]=true;m->cells.raw_mv[i]=(i==0)?4100:4200;m->cells.last_update_ms[i]=now_ms;}
#endif
 return AMS_ADBMS_RESULT_OK;
}
void ams_adbms_monitor_interrupt(ams_adbms_monitor_t *m,ams_adbms_result_t reason)
{ assert(reason!=AMS_ADBMS_RESULT_OK);++interrupt_calls;m->recovery.pending=true; }
ams_adbms_result_t ams_adbms_monitor_recovery_step(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint32_t now)
{ (void)io;(void)now;++recovery_calls;
 if(recovery_fail_stub)return AMS_ADBMS_RESULT_TRANSPORT_IO;
 if(recovery_pending_stub){m->recovery.pending=true;return AMS_ADBMS_RESULT_TRANSPORT_IO;}
 m->recovery.pending=false;return AMS_ADBMS_RESULT_OK;
}
ams_adbms_result_t ams_adbms_monitor_temperature(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint32_t now_ms)
{ (void)io;
#ifdef CONFIG_AMS_Z022_MEASUREMENT_VALIDATION
 assert(boundary_calls==acquire_calls);
#endif
 ++temp_calls; m->temperature.image.fresh_mask=0x10101U;
 m->temperature.image.last_update_ms[0]=now_ms; return AMS_ADBMS_RESULT_OK; }
ams_adbms_result_t ams_adbms_monitor_aux2(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint8_t sensor)
{ (void)m; (void)io; assert(sensor==(aux2_calls%24));++aux2_calls;return AMS_ADBMS_RESULT_OK; }
ams_adbms_result_t ams_adbms_monitor_therm_ow(ams_adbms_monitor_t *m,
 const ams_adbms_monitor_io_t *io,uint8_t sensor)
{ (void)m; (void)io; assert(sensor==(ow_calls%24));++ow_calls;return AMS_ADBMS_RESULT_OK; }
void ams_adbms_monitor_snapshot(const ams_adbms_monitor_t *m,ams_adbms_monitor_snapshot_t *s)
{
 memset(s,0,sizeof(*s));s->state=m->state;s->initialized=m->initialized;s->config_verified=m->config_verified;
 s->startup_post_passed=m->startup_post_passed;s->acquisition_live=m->acquisition_live;s->last_result=m->last_result;
 s->balance_inhibit_attempt_count=m->balance_inhibit_attempt_count;
 s->balance_inhibit_fail_count=m->balance_inhibit_fail_count;
 s->balance_mute_verified=m->balance_mute_verified;
 s->balance_durable_zero_verified=m->balance_durable_zero_verified;
 s->scan_attempt_count=m->scan_attempt_count;s->scan_success_count=m->scan_success_count;s->attempted_ms=m->attempted_ms;
 s->temperature=m->temperature;s->recovery=m->recovery;
 s->successful_ms=m->successful_ms;s->cells=m->cells;
}

int main(void)
{
 ams_adbms_monitor_platform_snapshot_t s;
 fake_spi_reset();
 assert(!ams_adbms_monitor_platform_snapshot(&s));
 assert(ams_adbms_monitor_platform_init_owner());
 assert(binds==1U && init_calls==1U && wakes==1U && writes==1U && reads==1U);
 assert(fake_cycles>=3000U*216U);
 assert(ams_adbms_monitor_platform_snapshot(&s));
 assert(s.owner_bound && s.init_attempted && s.initialized && s.acquisition_live && !s.physical_validated);
 assert(s.balance_inhibit_attempt_count==1U && s.balance_inhibit_fail_count==0U);
 assert(s.balance_mute_verified && s.balance_durable_zero_verified);
 ams_adbms_monitor_platform_step(1234U);
 assert(acquire_calls==1U && writes==2U);
 assert(ams_adbms_monitor_platform_snapshot(&s));
 assert(s.scan_success_count==1U && s.successful_ms==1234U && s.cells.usable_mask==0x7fffU);
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
 assert(s.balance_shadow.valid && s.balance_shadow.candidate_mask==0x1e);
 assert(s.balance_shadow.evaluated_ms==1234U);
#else
 assert(!s.balance_shadow.valid && s.balance_shadow.candidate_mask==0);
#endif
 assert(!ams_adbms_monitor_platform_init_owner());
 fake_isr=true;ams_adbms_monitor_platform_step(2000U);fake_isr=false;
 assert(acquire_calls==1U);
 fake_current_thread=2U;ams_adbms_monitor_platform_step(2000U);fake_current_thread=1U;
 assert(acquire_calls==1U);
#if defined(CONFIG_AMS_Z018_TEMP_VALIDATION) && CONFIG_AMS_Z018_TEMP_VALIDATION
 assert(temp_calls==1U && s.temperature.image.fresh_mask==0x10101U);
 unsigned a=aux2_calls,o=ow_calls;
 fake_spi.now_ms=249U;ams_adbms_monitor_platform_step(249U);
 assert(aux2_calls==a && ow_calls==o);
 fake_spi.now_ms=250U;ams_adbms_monitor_platform_step(250U);
 assert(aux2_calls==a+CONFIG_AMS_Z018_AUX2_DIAGNOSTIC);
 fake_spi.now_ms=1999U;ams_adbms_monitor_platform_step(1999U);assert(ow_calls==o);
 fake_spi.now_ms=2000U;ams_adbms_monitor_platform_step(2000U);
 assert(ow_calls==o+CONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC);
 a=aux2_calls;o=ow_calls;
 fake_spi.now_ms=100000U;ams_adbms_monitor_platform_step(100000U);
 assert(aux2_calls==a+CONFIG_AMS_Z018_AUX2_DIAGNOSTIC && ow_calls==o+CONFIG_AMS_Z018_THERM_OW_DIAGNOSTIC);
 a=aux2_calls;o=ow_calls;
 fake_spi.now_ms=100001U;ams_adbms_monitor_platform_step(100001U);
 assert(aux2_calls==a && ow_calls==o);
#else
 assert(temp_calls==0 && aux2_calls==0 && ow_calls==0);
#endif
#if defined(CONFIG_AMS_Z019_RECOVERY_VALIDATION) && CONFIG_AMS_Z019_RECOVERY_VALIDATION
 unsigned old_acquire=acquire_calls,old_temp=temp_calls;
 recovery_pending_stub=true;ams_adbms_monitor_platform_step(100100);
 assert(acquire_calls==old_acquire && temp_calls==old_temp);
#if defined(CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION) && CONFIG_AMS_Z020_BALANCE_DISABLED_VALIDATION
 assert(ams_adbms_monitor_platform_snapshot(&s));assert(!s.balance_shadow.valid && !s.balance_shadow.candidate_mask);
#endif
 recovery_pending_stub=false;ams_adbms_monitor_platform_step(100200);
 assert(acquire_calls==old_acquire && temp_calls==old_temp); /* recovered: no double scan */
 recovery_fail_stub=true;ams_adbms_monitor_platform_step(100300);
 assert(acquire_calls==old_acquire && temp_calls==old_temp);recovery_fail_stub=false;
 cell_fail_stub=true;ams_adbms_monitor_platform_step(100400);
 assert(interrupt_calls==1 && temp_calls==old_temp);cell_fail_stub=false;
 assert(recovery_calls>0);
#else
 assert(!recovery_calls && !interrupt_calls);
 (void)recovery_pending_stub;(void)recovery_fail_stub;
#endif
 puts("PASS Z017/Z018/Z019 Zephyr monitor adapter: owner-only String-B seam, cooperative timing, copied snapshot");
 return 0;
}
