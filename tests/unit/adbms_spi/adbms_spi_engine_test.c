#include "adbms_spi_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; \
} } while (0)

struct fake_spi {
    uint32_t now;
    uint32_t start_time;
    uint32_t tx_ready_after;
    uint32_t rx_ready_after;
    uint32_t busy_until_after;
    uint32_t fault_after;
    bool fault_enabled;
    bool start_fail;
    bool stop_fail;
    bool recovery_fail;
    bool active_cs_fail_once;
    bool inactive_cs_fail_once;
    bool started;
    bool cs_a;
    bool cs_b;
    unsigned int cs_assert_count;
    unsigned int cs_deassert_count;
    unsigned int start_count;
    unsigned int stop_count;
    unsigned int recovery_count;
    unsigned int read_count;
    uint8_t last_write;
    uint8_t tx_log[520];
    size_t tx_count;
};

static uint32_t elapsed(const struct fake_spi *f)
{
    return (uint32_t)(f->now - f->start_time);
}

static uint32_t fake_now(void *context)
{
    struct fake_spi *f = context;
    uint32_t value = f->now;
    f->now++;
    return value;
}

static int fake_cs(void *context, ams_adbms_spi_string_t string, bool active)
{
    struct fake_spi *f = context;
    if (string != AMS_ADBMS_SPI_STRING_A && string != AMS_ADBMS_SPI_STRING_B) {
        return -1;
    }
    if (active && f->active_cs_fail_once) {
        f->active_cs_fail_once = false;
        return -1;
    }
    if (!active && f->inactive_cs_fail_once) {
        f->inactive_cs_fail_once = false;
        return -1;
    }
    if (string == AMS_ADBMS_SPI_STRING_A) {
        f->cs_a = active;
    } else {
        f->cs_b = active;
    }
    if (active) {
        f->cs_assert_count++;
    } else {
        f->cs_deassert_count++;
    }
    return 0;
}

static int fake_start(void *context)
{
    struct fake_spi *f = context;
    f->start_count++;
    if (f->start_fail) {
        return -1;
    }
    f->started = true;
    return 0;
}

static bool fake_tx_ready(void *context)
{
    struct fake_spi *f = context;
    return elapsed(f) >= f->tx_ready_after;
}

static bool fake_rx_ready(void *context)
{
    struct fake_spi *f = context;
    return elapsed(f) >= f->rx_ready_after;
}

static bool fake_busy(void *context)
{
    struct fake_spi *f = context;
    return elapsed(f) < f->busy_until_after;
}

static bool fake_fault(void *context)
{
    struct fake_spi *f = context;
    return f->fault_enabled && elapsed(f) >= f->fault_after;
}

static int fake_write(void *context, uint8_t value)
{
    struct fake_spi *f = context;
    if (!f->started || f->tx_count >= sizeof(f->tx_log)) {
        return -1;
    }
    f->last_write = value;
    f->tx_log[f->tx_count++] = value;
    return 0;
}

static int fake_read(void *context, uint8_t *value)
{
    struct fake_spi *f = context;
    if (!f->started || value == NULL) {
        return -1;
    }
    *value = (uint8_t)(f->last_write ^ 0x5AU);
    f->read_count++;
    return 0;
}

static int fake_stop(void *context)
{
    struct fake_spi *f = context;
    f->stop_count++;
    if (f->stop_fail) {
        return -1;
    }
    f->started = false;
    return 0;
}

static int fake_recover(void *context)
{
    struct fake_spi *f = context;
    f->recovery_count++;
    f->started = false;
    f->cs_a = false;
    f->cs_b = false;
    return f->recovery_fail ? -1 : 0;
}

static struct ams_adbms_spi_backend make_backend(struct fake_spi *f)
{
    const struct ams_adbms_spi_backend backend = {
        .context = f,
        .now_ms = fake_now,
        .set_cs_active = fake_cs,
        .start = fake_start,
        .tx_ready = fake_tx_ready,
        .rx_ready = fake_rx_ready,
        .busy = fake_busy,
        .fault = fake_fault,
        .write_byte = fake_write,
        .read_byte = fake_read,
        .stop = fake_stop,
        .recover = fake_recover,
    };
    return backend;
}

static void reset_fake(struct fake_spi *f, uint32_t now)
{
    memset(f, 0, sizeof(*f));
    f->now = now;
    f->start_time = now;
}

int main(void)
{
    struct ams_adbms_spi_engine_config cfg = {
        .timeout_ms = 500U,
        .max_transfer_bytes = 512U,
        .read_dummy_byte = 0xFFU,
    };
    struct fake_spi f;
    struct ams_adbms_spi_backend b;
    uint8_t tx[512];
    uint8_t rx[511];
    ams_adbms_spi_result_t r;

    for (size_t i = 0U; i < sizeof(tx); ++i) {
        tx[i] = (uint8_t)i;
    }

    reset_fake(&f, 0U);
    b = make_backend(&f);
    EXPECT(ams_adbms_spi_engine_write(NULL, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U) ==
           AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT);
    EXPECT(ams_adbms_spi_engine_write(&b, &cfg, (ams_adbms_spi_string_t)2, tx, 1U) ==
           AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT);
    EXPECT(ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, NULL, 1U) ==
           AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT);
    EXPECT(ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 0U) ==
           AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT);
    EXPECT(f.cs_assert_count == 0U && f.start_count == 0U);

    reset_fake(&f, 10U);
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 4U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_OK);
    EXPECT(f.tx_count == 4U && f.read_count == 4U);
    EXPECT(memcmp(f.tx_log, tx, 4U) == 0);
    EXPECT(!f.cs_a && !f.cs_b && !f.started);
    EXPECT(f.cs_assert_count == 1U && f.cs_deassert_count == 1U);
    EXPECT(f.recovery_count == 0U);


    reset_fake(&f, 25U);
    b = make_backend(&f);
    f.active_cs_fail_once = true;
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_IO_ERROR);
    EXPECT(f.recovery_count == 1U);
    EXPECT(!f.cs_a && !f.cs_b && !f.started);

    reset_fake(&f, 20U);
    b = make_backend(&f);
    memset(rx, 0, 3U);
    r = ams_adbms_spi_engine_write_read(&b, &cfg, AMS_ADBMS_SPI_STRING_B,
                                        tx, 2U, rx, 3U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_OK);
    EXPECT(f.tx_count == 5U && f.read_count == 5U);
    EXPECT(f.tx_log[0] == tx[0] && f.tx_log[1] == tx[1]);
    EXPECT(f.tx_log[2] == 0xFFU && f.tx_log[3] == 0xFFU && f.tx_log[4] == 0xFFU);
    EXPECT(rx[0] == 0xA5U && rx[1] == 0xA5U && rx[2] == 0xA5U);
    EXPECT(!f.cs_a && !f.cs_b);

    /* Exact BUFSZ boundary: tx_len + rx_len == 512 is valid; 513 is not. */
    reset_fake(&f, 30U);
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write_read(&b, &cfg, AMS_ADBMS_SPI_STRING_A,
                                        tx, 1U, rx, 511U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_OK && f.tx_count == 512U);
    reset_fake(&f, 30U);
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write_read(&b, &cfg, AMS_ADBMS_SPI_STRING_A,
                                        tx, 2U, rx, 511U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_INVALID_ARGUMENT);
    EXPECT(f.cs_assert_count == 0U);

    /* HAL-style ordering checks the ready flag before timeout. Ready at the
     * exact 500 ms boundary is accepted; still-not-ready at that boundary is
     * timed out. */
    reset_fake(&f, 100U);
    f.tx_ready_after = 500U;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_OK);
    reset_fake(&f, 100U);
    f.tx_ready_after = 501U;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_TIMEOUT);
    EXPECT(f.recovery_count == 1U && !f.cs_a && !f.cs_b && !f.started);

    reset_fake(&f, 0U);
    f.rx_ready_after = 501U;
    b = make_backend(&f);
    memset(rx, 0xCC, 1U);
    r = ams_adbms_spi_engine_write_read(&b, &cfg, AMS_ADBMS_SPI_STRING_B,
                                        tx, 1U, rx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_TIMEOUT);
    EXPECT(rx[0] == 0U && f.recovery_count == 1U);

    reset_fake(&f, 0U);
    f.busy_until_after = 501U;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_TIMEOUT);
    EXPECT(f.recovery_count == 1U && !f.cs_a && !f.started);

    reset_fake(&f, 0U);
    f.fault_enabled = true;
    f.fault_after = 0U;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_IO_ERROR);
    EXPECT(f.recovery_count == 1U && !f.cs_a && !f.started);

    reset_fake(&f, 0U);
    f.start_fail = true;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_IO_ERROR && f.recovery_count == 1U);

    reset_fake(&f, 0U);
    f.stop_fail = true;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_IO_ERROR && f.recovery_count == 1U);

    reset_fake(&f, 0U);
    f.tx_ready_after = 501U;
    f.recovery_fail = true;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_RECOVERY_FAILED);
    EXPECT(!f.cs_a && !f.cs_b && !f.started);

    /* A deassert error itself invokes recovery; successful recovery preserves
     * fail-safe CS state while the operation is reported as an I/O failure. */
    reset_fake(&f, 0U);
    f.inactive_cs_fail_once = true;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_B, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_IO_ERROR);
    EXPECT(f.recovery_count == 1U && !f.cs_a && !f.cs_b);

    /* Wrap-safe absolute elapsed timing. */
    reset_fake(&f, UINT32_MAX - 250U);
    f.tx_ready_after = 300U;
    b = make_backend(&f);
    r = ams_adbms_spi_engine_write(&b, &cfg, AMS_ADBMS_SPI_STRING_A, tx, 1U);
    EXPECT(r == AMS_ADBMS_SPI_RESULT_OK);

    printf("PASS Z-015 ADBMS SPI engine directed checks\n");
    return 0;
}
