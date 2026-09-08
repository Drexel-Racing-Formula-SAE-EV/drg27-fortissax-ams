#include "adbms_spi_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_spi {
    uint32_t now;
    uint32_t start;
    unsigned int scenario;
    unsigned int fault_calls;
    bool started;
    bool cs_a;
    bool cs_b;
    bool recovery_fail;
    uint8_t last_write;
    uint8_t tx_log[32];
    size_t tx_count;
    unsigned int recovery_count;
};

static uint32_t elapsed(const struct fake_spi *f) { return (uint32_t)(f->now - f->start); }
static uint32_t now_ms(void *c) { struct fake_spi *f=c; uint32_t n=f->now; f->now++; return n; }
static int cs(void *c, ams_adbms_spi_string_t s, bool a) {
    struct fake_spi *f=c;
    if (s == AMS_ADBMS_SPI_STRING_A) f->cs_a=a;
    else if (s == AMS_ADBMS_SPI_STRING_B) f->cs_b=a;
    else return -1;
    return 0;
}
static int start(void *c) { struct fake_spi *f=c; if (f->scenario==5U) return -1; f->started=true; return 0; }
static bool tx_ready(void *c) { struct fake_spi *f=c; return (f->scenario==1U || f->scenario==7U) ? false : true; }
static bool rx_ready(void *c) { struct fake_spi *f=c; return f->scenario==2U ? false : true; }
static bool busy(void *c) { struct fake_spi *f=c; return f->scenario==3U ? elapsed(f)<4U : false; }
static bool fault(void *c) { struct fake_spi *f=c; f->fault_calls++; return f->scenario==4U && f->fault_calls>1U; }
static int write_byte(void *c, uint8_t v) { struct fake_spi *f=c; if (!f->started || f->tx_count>=sizeof(f->tx_log)) return -1; f->last_write=v; f->tx_log[f->tx_count++]=v; return 0; }
static int read_byte(void *c, uint8_t *v) { struct fake_spi *f=c; if (!f->started || v==NULL) return -1; *v=(uint8_t)(f->last_write+1U); return 0; }
static int stop(void *c) { struct fake_spi *f=c; if (f->scenario==6U) return -1; f->started=false; return 0; }
static int recover(void *c) { struct fake_spi *f=c; f->recovery_count++; f->started=false; f->cs_a=f->cs_b=false; return f->recovery_fail?-1:0; }

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x=*state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5; *state=x; return x;
}

static void die(unsigned long i, const char *why)
{
    fprintf(stderr, "FAIL randomized op %lu: %s\n", i, why);
    exit(1);
}

int main(int argc, char **argv)
{
    uint32_t rng = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 0xC0FFEEU;
    unsigned long iterations = argc > 2 ? strtoul(argv[2], NULL, 0) : 200000UL;
    const struct ams_adbms_spi_engine_config cfg = { .timeout_ms=3U, .max_transfer_bytes=16U, .read_dummy_byte=0xFFU };

    for (unsigned long i=0; i<iterations; ++i) {
        struct fake_spi f;
        uint8_t tx[8], rx[8];
        size_t tx_len=(size_t)(xorshift32(&rng)%7U)+1U;
        size_t rx_len=(size_t)(xorshift32(&rng)%(9U-tx_len));
        unsigned int scenario=(unsigned int)(xorshift32(&rng)%8U);
        ams_adbms_spi_string_t string=(xorshift32(&rng)&1U)?AMS_ADBMS_SPI_STRING_A:AMS_ADBMS_SPI_STRING_B;
        struct ams_adbms_spi_backend b;
        ams_adbms_spi_result_t result;
        ams_adbms_spi_result_t expected;

        memset(&f,0,sizeof(f));
        f.now=xorshift32(&rng); f.start=f.now; f.scenario=scenario;
        f.recovery_fail=(scenario==7U);
        for (size_t j=0;j<sizeof(tx);++j) tx[j]=(uint8_t)(xorshift32(&rng)&0xFFU);
        memset(rx,0xCC,sizeof(rx));
        b=(struct ams_adbms_spi_backend){
            .context=&f,.now_ms=now_ms,.set_cs_active=cs,.start=start,
            .tx_ready=tx_ready,.rx_ready=rx_ready,.busy=busy,.fault=fault,
            .write_byte=write_byte,.read_byte=read_byte,.stop=stop,.recover=recover};

        if (rx_len != 0U) result=ams_adbms_spi_engine_write_read(&b,&cfg,string,tx,tx_len,rx,rx_len);
        else result=ams_adbms_spi_engine_write(&b,&cfg,string,tx,tx_len);

        switch (scenario) {
        case 0U: expected=AMS_ADBMS_SPI_RESULT_OK; break;
        case 1U: case 2U: case 3U: expected=AMS_ADBMS_SPI_RESULT_TIMEOUT; break;
        case 4U: case 5U: case 6U: expected=AMS_ADBMS_SPI_RESULT_IO_ERROR; break;
        case 7U: expected=AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED; break;
        default: die(i,"bad scenario"); return 2;
        }
        if (result != expected) die(i,"unexpected result");
        if (f.cs_a || f.cs_b || f.started) die(i,"ownership not returned to idle");

        if (result == AMS_ADBMS_SPI_RESULT_OK) {
            if (f.tx_count != tx_len+rx_len) die(i,"wire length mismatch");
            for (size_t j=0;j<tx_len;++j) if (f.tx_log[j]!=tx[j]) die(i,"tx prefix mismatch");
            for (size_t j=tx_len;j<tx_len+rx_len;++j) if (f.tx_log[j]!=0xFFU) die(i,"dummy byte mismatch");
            for (size_t j=0;j<rx_len;++j) if (rx[j]!=0U) {
                /* dummy 0xFF + 1 wraps to zero */
                die(i,"read suffix mismatch");
            }
            if (f.recovery_count != 0U) die(i,"recovery on success");
        } else {
            if (scenario>=1U && scenario<=7U && f.recovery_count != 1U) die(i,"missing single recovery");
            for (size_t j=0;j<rx_len;++j) if (rx[j]!=0U) die(i,"failed read not zeroed");
        }
    }

    printf("PASS Z-015 ADBMS SPI randomized model: %lu operations seed=0x%08x\n",
           iterations, rng);
    return 0;
}
