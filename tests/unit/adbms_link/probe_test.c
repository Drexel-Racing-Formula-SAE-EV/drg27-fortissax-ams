#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ams_platform/adbms_link_probe.h>
#include <ams_platform/adbms_spi_lifecycle.h>
#include <ams_core/ams_adbms_link.h>
#include "adbms_spi_internal.h"
uintptr_t fake_current_thread=1;
bool fake_isr,fake_clock_stalled;
uint64_t fake_cycles;
static unsigned wakes,reads,binds;
static bool corrupt;
bool ams_adbms_time_now(uint64_t *us){*us=fake_cycles/216U;return true;}
bool ams_adbms_spi_bind_owner(void){++binds;return true;}
bool ams_adbms_spi_wake_b(bool cold){wakes++;fake_cycles+=(cold?4000U:2000U)*216U;return true;}
ams_adbms_spi_platform_status_t ams_adbms_spi_platform_status(void){
 ams_adbms_spi_platform_status_t s={.state=AMS_ADBMS_SPI_PLATFORM_READY};return s;
}
ams_adbms_spi_result_t ams_adbms_spi_write_read(ams_adbms_spi_string_t string,
 const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn){
 assert(string==AMS_ADBMS_SPI_STRING_B && tn==4 && rn==8);
 assert(tx[0]==0 && (tx[1]==2 || tx[1]==0x2c));
 reads++;memset(rx,0,8);rx[1]=6;
 uint16_t p=ams_link_pec10(rx,0);rx[6]=(uint8_t)(p>>8);rx[7]=(uint8_t)p;
 if(corrupt)rx[7]^=1;
 fake_cycles+=300U*216U;
 return AMS_ADBMS_SPI_RESULT_OK;
}
int main(int argc,char **argv){
 (void)argv;corrupt=argc>1;
 for(unsigned i=0;i<20;i++){ams_adbms_link_probe_step();fake_cycles+=100000U*216U;}
 assert(binds==1);
 assert(reads==(corrupt?1U:4U));assert(wakes==(corrupt?2U:5U));
 puts("PASS Z016 production finite probe: allowed frames, stop-on-error, no repeat after completion");
}
