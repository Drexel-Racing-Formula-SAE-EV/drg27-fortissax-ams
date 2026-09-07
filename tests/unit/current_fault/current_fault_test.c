#include <ams_core/ams_current_fault.h>

#include <math.h>
#include <stdio.h>

static unsigned checks;
static unsigned failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); } } while(0)

static void repeat(current_fault_state_t *f, current_fault_mode_t mode,
                   float current, bool valid, current_sensor_reason_t reason,
                   unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        current_fault_update(f, mode, current, valid, reason, 20u);
    }
}

static void test_discharge_edges(void)
{
    current_fault_state_t f;
    current_fault_init(&f);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 69.999f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(!f.warning && !f.pending && !f.confirmed);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 70.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.warning && f.reason == CURRENT_FAULT_REASON_DISCHARGE_WARNING);

    current_fault_init(&f);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 85.0f, true, CURRENT_SENSOR_REASON_OK, 24u);
    CHECK(f.pending && !f.confirmed && f.pending_ms == 480u);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 85.0f, true, CURRENT_SENSOR_REASON_OK, 1u);
    CHECK(f.confirmed && f.latched && f.pending_ms == 500u);
    CHECK(f.latched_reason == CURRENT_FAULT_REASON_DISCHARGE_OVERCURRENT);

    current_fault_init(&f);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 120.0f, true, CURRENT_SENSOR_REASON_OK, 4u);
    CHECK(f.pending && !f.confirmed && f.pending_ms == 80u);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 120.0f, true, CURRENT_SENSOR_REASON_OK, 1u);
    CHECK(f.confirmed && f.latched_reason == CURRENT_FAULT_REASON_DISCHARGE_FAST_OVERCURRENT);

    current_fault_init(&f);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 240.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.confirmed && f.latched_reason == CURRENT_FAULT_REASON_DISCHARGE_EXTREME);
}

static void test_charge_regen_precharge(void)
{
    current_fault_state_t f;
    current_fault_init(&f);
    repeat(&f, CURRENT_FAULT_MODE_CHARGE, -12.0f, true, CURRENT_SENSOR_REASON_OK, 24u);
    CHECK(!f.confirmed && f.pending_ms == 480u);
    repeat(&f, CURRENT_FAULT_MODE_CHARGE, -12.0f, true, CURRENT_SENSOR_REASON_OK, 1u);
    CHECK(f.confirmed && f.latched_reason == CURRENT_FAULT_REASON_CHARGE_OVERCURRENT);

    current_fault_init(&f);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, -5.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.warning && f.reason == CURRENT_FAULT_REASON_REGEN_UNEXPECTED);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, -20.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.warning && f.reason == CURRENT_FAULT_REASON_REGEN_WARNING);

    current_fault_init(&f);
    repeat(&f, CURRENT_FAULT_MODE_PRECHARGE, 1.2f, true, CURRENT_SENSOR_REASON_OK, 9u);
    CHECK(f.pending && !f.confirmed && f.pending_ms == 180u);
    repeat(&f, CURRENT_FAULT_MODE_PRECHARGE, 1.2f, true, CURRENT_SENSOR_REASON_OK, 1u);
    CHECK(f.confirmed && f.latched_reason == CURRENT_FAULT_REASON_PRECHARGE_OVERCURRENT);

    current_fault_init(&f);
    current_fault_update(&f, CURRENT_FAULT_MODE_PRECHARGE, 2.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(!f.confirmed && f.pending_ms == 20u);
    current_fault_update(&f, CURRENT_FAULT_MODE_PRECHARGE, 2.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.confirmed && f.latched_reason == CURRENT_FAULT_REASON_PRECHARGE_FAST_OVERCURRENT);
}

static void test_sensor_fault_and_recovery(void)
{
    current_fault_state_t f;
    current_fault_init(&f);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 20.0f, false, CURRENT_SENSOR_REASON_ADC_READ, 24u);
    CHECK(!f.sensor_fault && f.sensor_invalid_ms == 480u);
    repeat(&f, CURRENT_FAULT_MODE_DRIVE, 20.0f, false, CURRENT_SENSOR_REASON_ADC_READ, 1u);
    CHECK(f.sensor_fault && f.sensor_invalid_ms == 500u);
    CHECK(f.reason == CURRENT_FAULT_REASON_SENSOR_ADC_READ);

    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 0.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(!f.sensor_fault && f.sensor_invalid_ms == 0u);

    current_fault_init(&f);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 75.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(f.warning);
    current_fault_update(&f, CURRENT_FAULT_MODE_DRIVE, 0.0f, true, CURRENT_SENSOR_REASON_OK, 20u);
    CHECK(!f.warning && !f.pending && !f.confirmed && !f.latched);
}

int main(void)
{
    test_discharge_edges();
    test_charge_regen_precharge();
    test_sensor_fault_and_recovery();
    printf("current fault: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
