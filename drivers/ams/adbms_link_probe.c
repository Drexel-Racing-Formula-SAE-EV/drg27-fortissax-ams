#include <ams_platform/adbms_link_probe.h>
#include <ams_platform/adbms_spi_lifecycle.h>
#include <ams_core/ams_adbms_link.h>
#include "adbms_spi_internal.h"
#include "adbms_time_internal.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
static bool now_us(void *ctx,uint64_t *us)
{
 (void)ctx;return ams_adbms_time_now(us);
}
static bool wake_link(void *ctx,bool cold)
{
 (void)ctx;
 if (!ams_adbms_spi_wake_b(cold)) return false;
 if (cold) k_sleep(K_MSEC(5));
 return true;
}
static bool read_link(void *ctx,const uint8_t cmd[4],uint8_t rx[8])
{
 (void)ctx;
 return ams_adbms_spi_write_read(AMS_ADBMS_SPI_STRING_B,cmd,4U,rx,8U)
        ==AMS_ADBMS_SPI_RESULT_OK;
}
void ams_adbms_link_probe_step(void)
{
 static unsigned step;
 static ams_link_t link;
 const ams_link_io_t io={.now_us=now_us,.wake=wake_link,.read=read_link};
 ams_link_sample_t sample;
 ams_link_result_t result;
 if (step>=4U) return;
 if (step==0U && !ams_adbms_spi_bind_owner()) { step=4U;return; }
 if (ams_adbms_spi_platform_status().state!=AMS_ADBMS_SPI_PLATFORM_READY) {
  printk("Z016 String B probe: link unavailable; stopped\n");step=4U;return;
 }
 if (step==0U) result=ams_link_wake(&link,&io,false);
 else if (step==1U) result=ams_link_read(&link,&io,AMS_LINK_SID,false,&sample);
 else if (step==2U) {
  /* One awake session: standalone CFGA followed immediately by guarded SID.
   * Logging occurs only after the pair, outside its 3 ms guard. */
  result=ams_link_read(&link,&io,AMS_LINK_CFGA,false,&sample);
  if (result==AMS_LINK_OK) result=ams_link_read(&link,&io,AMS_LINK_SID,true,&sample);
 } else {
  result=ams_link_wake(&link,&io,true);
  if (result==AMS_LINK_OK) result=ams_link_read(&link,&io,AMS_LINK_SID,false,&sample);
 }
 printk("Z016 String B / 1 SMB probe step %u result %d\n",step,(int)result);
 ++step;
 if (result!=AMS_LINK_OK) step=4U;
}
