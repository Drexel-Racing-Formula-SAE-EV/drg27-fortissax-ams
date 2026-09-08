#include <ams_platform/adbms_spi_lifecycle.h>
#include "adbms_spi_internal.h"
#include "fake_zephyr/fake_zephyr_spi.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if(!(x)){ failures++; fprintf(stderr,"FAIL:%d: %s\n",__LINE__,#x); } } while(0)
static uint32_t rng=0x615A015u;
static uint32_t rnd(void){uint32_t x=rng; x^=x<<13; x^=x>>17; x^=x<<5; return rng=x;}

static ams_adbms_spi_result_t reentry_result;
static uint8_t reentry_byte=0xA5U;
static void reentry_hook(void){
    reentry_result=ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_B,&reentry_byte,1U);
}
static void expect_ready(void){
    ams_adbms_spi_platform_status_t s=ams_adbms_spi_platform_status();
    CHECK(s.state==AMS_ADBMS_SPI_PLATFORM_READY); CHECK(s.input_clock_hz==108000000U);
    CHECK(s.achieved_clock_hz==421875U); CHECK(s.timeout_ms==500U); CHECK(s.max_transfer_bytes==512U);
    CHECK(s.cs_idle_guaranteed); CHECK(s.irq_path_disabled); CHECK(!fake_spi.cs_a_active&&!fake_spi.cs_b_active);
    CHECK(!fake_spi.irq_enabled);
}
int main(int argc,char**argv){
    const char *scenario=argc>1?argv[1]:"nominal"; uint8_t tx[8]={0x12,0x34,0x56,0x78,1,2,3,4}; uint8_t rx[4]={0};
    fake_spi_reset();
    if(!strcmp(scenario,"clock-bad")) fake_spi.clock_rate=54000000U;
    if(!strcmp(scenario,"pinctrl-bad")) fake_spi.pinctrl_fail=true;
    if(!strcmp(scenario,"reset-init-bad")) fake_spi.reset_fail=true;
    int ret=ams_adbms_spi_platform_init();
    if(!strcmp(scenario,"clock-bad")||!strcmp(scenario,"pinctrl-bad")||!strcmp(scenario,"reset-init-bad")){
        CHECK(ret!=0); CHECK(ams_adbms_spi_platform_status().state==AMS_ADBMS_SPI_PLATFORM_FAULTED);
        CHECK(!fake_spi.cs_a_active&&!fake_spi.cs_b_active); CHECK(!fake_spi.irq_enabled); CHECK(fake_spi.tx_count==0U);
        goto done;
    }
    CHECK(ret==0); CHECK(fake_spi.tx_count==0U); CHECK(fake_spi.reset_count==1U); CHECK(fake_spi.irq_clear_count>=2U); expect_ready();
    if(!strcmp(scenario,"nominal")){
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,4)==AMS_ADBMS_SPI_RESULT_OK);
        CHECK(fake_spi.tx_count==4U&&fake_spi.rx_count==4U); expect_ready();
        unsigned before=fake_spi.tx_count; memset(rx,0,sizeof(rx));
        CHECK(ams_adbms_spi_write_read(AMS_ADBMS_SPI_STRING_B,tx,2,rx,3)==AMS_ADBMS_SPI_RESULT_OK);
        CHECK(fake_spi.tx_count==before+5U); CHECK(fake_spi.tx_log[before]==0x12&&fake_spi.tx_log[before+1]==0x34);
        CHECK(fake_spi.tx_log[before+2]==0xFF&&fake_spi.tx_log[before+3]==0xFF&&fake_spi.tx_log[before+4]==0xFF);
        expect_ready();
    } else if(!strcmp(scenario,"tx-timeout")){
        fake_spi.txe_stuck=true; CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_TIMEOUT);
        CHECK(fake_spi.reset_count==2U); expect_ready(); fake_spi.txe_stuck=false;
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_OK); expect_ready();
    } else if(!strcmp(scenario,"rx-timeout")){
        fake_spi.rxne_stuck=true; CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_TIMEOUT);
        CHECK(fake_spi.reset_count==2U); expect_ready();
    } else if(!strcmp(scenario,"bsy-timeout")){
        fake_spi.bsy_stuck=true; CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_TIMEOUT);
        CHECK(fake_spi.reset_count==2U); expect_ready();
    } else if(!strcmp(scenario,"fault")){
        fake_spi.fault_ovr=true; CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_IO_ERROR);
        CHECK(fake_spi.reset_count==2U); expect_ready();
    } else if(!strcmp(scenario,"cs-assert")){
        fake_spi.cs_assert_fail_once=true; CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_IO_ERROR);
        CHECK(fake_spi.reset_count==2U); expect_ready();
    } else if(!strcmp(scenario,"recovery-fail")){
        fake_spi.txe_stuck=true; fake_spi.reset_fail=true;
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED);
        CHECK(ams_adbms_spi_platform_status().state==AMS_ADBMS_SPI_PLATFORM_FAULTED); CHECK(!fake_spi.cs_a_active&&!fake_spi.cs_b_active);
        uint32_t before_violation=ams_adbms_spi_platform_status().integrity_violation_count;
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT);
        CHECK(ams_adbms_spi_platform_status().integrity_violation_count==before_violation+1U);
    } else if(!strcmp(scenario,"reentry")){
        ams_adbms_spi_platform_status_t before=ams_adbms_spi_platform_status();
        reentry_result=AMS_ADBMS_SPI_RESULT_OK; fake_spi.txe_hook_fired=false; fake_spi.txe_hook=reentry_hook;
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_OK);
        fake_spi.txe_hook=NULL;
        CHECK(reentry_result==AMS_ADBMS_SPI_RESULT_INTERNAL_FAULT);
        CHECK(ams_adbms_spi_platform_status().integrity_violation_count==before.integrity_violation_count+1U);
        CHECK(ams_adbms_spi_platform_status().transfer_success_count==before.transfer_success_count+1U);
        expect_ready();
        CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==AMS_ADBMS_SPI_RESULT_OK);
        CHECK(ams_adbms_spi_platform_status().integrity_violation_count==before.integrity_violation_count+1U);
        expect_ready();
    } else if(!strcmp(scenario,"random")){
        unsigned n=argc>2?(unsigned)strtoul(argv[2],0,0):50000U;
        for(unsigned i=0;i<n;i++){
            fake_spi.txe_stuck=fake_spi.rxne_stuck=fake_spi.bsy_stuck=fake_spi.fault_ovr=false; fake_spi.cs_assert_fail_once=false;
            unsigned mode=rnd()%6U; ams_adbms_spi_result_t exp=AMS_ADBMS_SPI_RESULT_OK;
            if(mode==1){fake_spi.txe_stuck=true;exp=AMS_ADBMS_SPI_RESULT_TIMEOUT;}
            else if(mode==2){fake_spi.rxne_stuck=true;exp=AMS_ADBMS_SPI_RESULT_TIMEOUT;}
            else if(mode==3){fake_spi.bsy_stuck=true;exp=AMS_ADBMS_SPI_RESULT_TIMEOUT;}
            else if(mode==4){fake_spi.fault_ovr=true;exp=AMS_ADBMS_SPI_RESULT_IO_ERROR;}
            else if(mode==5){fake_spi.cs_assert_fail_once=true;exp=AMS_ADBMS_SPI_RESULT_IO_ERROR;}
            CHECK(ams_adbms_spi_write(AMS_ADBMS_SPI_STRING_A,tx,1)==exp); expect_ready();
        }
    } else { CHECK(0); }
done:
    printf("PASS Z-015 production SPI6 adapter SIL scenario=%s checks=%u failures=%u\n",scenario,checks,failures);
    return failures?1:0;
}
