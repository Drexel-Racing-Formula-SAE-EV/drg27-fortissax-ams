#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ams_platform/adbms_spi_lifecycle.h>
#include "adbms_spi_internal.h"
#include "adbms_time_internal.h"
#include "fake_zephyr_spi.h"
uintptr_t fake_current_thread=1;
bool fake_isr,fake_clock_stalled;
uint64_t fake_cycles;
int main(int argc,char **argv){
 fake_spi_reset();assert(ams_adbms_spi_platform_init()==0);
 assert(!ams_adbms_spi_wake_b(false));assert(fake_spi.cs_assert_count==0);
 assert(ams_adbms_spi_bind_owner());assert(!ams_adbms_spi_bind_owner());
 uint8_t tx=0;
 assert(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,&tx,1)==AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT);
 assert(fake_spi.tx_count==0);
 fake_current_thread=2;assert(!ams_adbms_spi_wake_b(false));
 assert(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_B,&tx,1)==AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT);
 fake_current_thread=1;fake_isr=true;assert(!ams_adbms_spi_wake_b(false));fake_isr=false;
 bool bad=argc>1 && !strcmp(argv[1],"stalled");
 bool csbad=argc>1 && !strcmp(argv[1],"cs-fault");
 fake_clock_stalled=bad;fake_spi.cs_assert_fail_once=csbad;
 unsigned before=fake_spi.cs_assert_count;
 assert(ams_adbms_spi_wake_b(true)==!(bad||csbad));
 assert(!fake_spi.cs_a_active && !fake_spi.cs_b_active && !fake_spi.regs.enabled);
 assert(fake_spi.tx_count==0);
 if (!bad && !csbad) {
  assert(fake_spi.cs_assert_count==before+2);
  assert(fake_cycles>=4000U*216U);
  assert(ams_adbms_spi_platform_status().state==AMS_ADBMS_SPI_PLATFORM_READY);
  assert(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_B,&tx,1)==AMS_ADBMS_SPI_RESULT_OK);
 } else assert(ams_adbms_spi_platform_status().state==AMS_ADBMS_SPI_PLATFORM_FAULTED);
 puts("PASS Z016 production wake/time: owner, String B, CS cleanup, finite timing failure");
}
