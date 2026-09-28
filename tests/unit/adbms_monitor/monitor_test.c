#include <ams_core/ams_adbms_monitor.h>
#include <ams_core/ams_adbms_protocol.h>
#include <ams_core/ams_cell_image.h>

#include <ams_core/ams_thermistor.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now_us;
    uint8_t counter;
    uint8_t cfga[6];
    uint8_t cfgb[6];
    uint8_t pwma[6];
    uint8_t pwmb[6];
    uint8_t comm[6], selected[3];
    bool comm_started, ack_bad, pre_comm_bad;
    unsigned stcomm_count;
    int last_conversion;
    int aux2_offset;
    bool ow_no_response;
    bool muted;
    bool snapshot;
    uint16_t ccts;
    unsigned wake_count;
    unsigned write_count;
    unsigned read_count;
    unsigned adcv_count;
    unsigned snap_count;
    unsigned unsnap_count;
    unsigned post_attempt_osc_fast;
    unsigned wrcfga_count;
    unsigned mute_count;
    unsigned wrpwma_count;
    unsigned wrpwmb_count;

    bool wrong_sid;
    bool config_readback_bad;
    bool mute_readback_bad;
    bool pwma_readback_bad;
    bool pwmb_readback_bad;
    unsigned post_fail_osc_fast_count;
    uint8_t post_unexpected_flag_once;
    unsigned fail_wrcfga_at;
    unsigned ccts_zero_count;
    int corrupt_command_once;
    int transport_fail_command_once;
    ams_adbms_result_t transport_fail_result;
    bool transport_fail_applies_remote;
    int clock_fail_after_write_command_once;
    bool clock_fail_pending;
    int arm_cleanup_wake_fail_after_command_once;
    unsigned wake_fail_count;
    unsigned fail_unsnap_count;
    unsigned fail_write_call;
    unsigned fail_read_call;
    int gap_after_command_once;
    uint32_t gap_us;
    int override_command_once;
    uint8_t override_slot;
    int16_t override_code;
    bool gap_pending;
    bool gap_skip_mark_activity;
    int write_trace[512];
    unsigned write_trace_count;
} fake_t;

static int command_from_frame(const uint8_t *frame, uint16_t length)
{
    if ((frame == NULL) || (length < 4U)) return -1;
    for (int c = 0; c < (int)AMS_ADBMS_CMD_COUNT; ++c) {
        ams_adbms_command_info_t info;
        if (!ams_adbms_command_info((ams_adbms_command_t)c, &info)) continue;
        if ((frame[0] == info.byte0) && (frame[1] == info.byte1)) return c;
    }
    return -1;
}

static void put16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    p[0] = (uint8_t)u;
    p[1] = (uint8_t)(u >> 8U);
}

static int16_t mv_status_code(int mv)
{
    return (int16_t)(((mv - 1500) * 1000) / 150);
}

static int16_t temp_code(int deci_c)
{
    return (int16_t)((deci_c + 2730) * 5 - 10000);
}

static int16_t cell_mv_code(int mv)
{
    int32_t uv = mv * 1000;
    return (int16_t)(((uv + 75) / 150) - 10000);
}

static void fake_init(fake_t *f)
{
    memset(f, 0, sizeof(*f));
    f->ccts = 100U;
    f->corrupt_command_once = -1;
    f->transport_fail_command_once = -1;
    f->clock_fail_after_write_command_once = -1;
    f->arm_cleanup_wake_fail_after_command_once = -1;
    f->gap_after_command_once = -1;
    f->override_command_once = -1;
    ams_adbms_z017_production_cfga(f->cfga);
    ams_adbms_z017_production_cfgb(f->cfgb);
}

static ams_adbms_result_t fake_now(void *ctx, uint64_t *now)
{
    fake_t *f = ctx;
    if (f->clock_fail_pending) {
        f->clock_fail_pending = false;
        return AMS_ADBMS_RESULT_CLOCK;
    }
    if (f->gap_pending) {
        if (f->gap_skip_mark_activity) {
            f->gap_skip_mark_activity = false;
        } else {
            f->now_us += f->gap_us;
            f->gap_pending = false;
        }
    }
    *now = f->now_us;
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t fake_delay(void *ctx, uint32_t delay_us)
{
    fake_t *f = ctx;
    f->now_us += delay_us;
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_result_t fake_wake(void *ctx, bool cold)
{
    fake_t *f = ctx;
    (void)cold;
    f->wake_count++;
    f->now_us += 100U;
    if (f->wake_fail_count > 0U) {
        f->wake_fail_count--;
        return AMS_ADBMS_RESULT_TRANSPORT_IO;
    }
    return AMS_ADBMS_RESULT_OK;
}

static bool validate_command_pec(const uint8_t *tx)
{
    uint16_t p = ams_adbms_pec15(tx, 2U);
    return tx[2] == (uint8_t)(p >> 8U) && tx[3] == (uint8_t)p;
}

static ams_adbms_result_t maybe_transport_fail(fake_t *f, int command)
{
    if (f->transport_fail_command_once == command) {
        f->transport_fail_command_once = -1;
        return f->transport_fail_result;
    }
    return AMS_ADBMS_RESULT_OK;
}

static void increment_counter(fake_t *f)
{
    f->counter = ams_adbms_counter_next(f->counter);
}

static ams_adbms_result_t fake_write(void *ctx, const uint8_t *tx, uint16_t tx_len)
{
    fake_t *f = ctx;
    int command = command_from_frame(tx, tx_len);
    ams_adbms_command_info_t info;
    ams_adbms_result_t fail;
    assert(command >= 0);
    assert(validate_command_pec(tx));
    assert(ams_adbms_command_info((ams_adbms_command_t)command, &info));
    f->write_count++;
    if (f->write_trace_count < (sizeof(f->write_trace) / sizeof(f->write_trace[0]))) {
        f->write_trace[f->write_trace_count++] = command;
    }
    f->now_us += 120U;
    if ((f->fail_write_call != 0U) && (f->write_count == f->fail_write_call)) {
        return AMS_ADBMS_RESULT_TRANSPORT_IO;
    }

    fail = maybe_transport_fail(f, command);
    if (fail != AMS_ADBMS_RESULT_OK) {
        if (f->transport_fail_applies_remote) {
            if (command == AMS_ADBMS_CMD_WRCFGA) {memcpy(f->cfga,&tx[4],6U);increment_counter(f);}
            if (command == AMS_ADBMS_CMD_STCOMM) {f->comm_started=true;f->stcomm_count++;increment_counter(f);}
            if (command == AMS_ADBMS_CMD_SNAP) { f->snapshot = true; f->snap_count++; }
            if (command == AMS_ADBMS_CMD_UNSNAP) { f->snapshot = false; f->unsnap_count++; }
            if (command == AMS_ADBMS_CMD_MUTE) { f->muted = true; f->mute_count++; }
            f->transport_fail_applies_remote = false;
        }
        return fail;
    }
    if (command == AMS_ADBMS_CMD_WRCFGA) {
        f->wrcfga_count++;
        if ((f->fail_wrcfga_at != 0U) && (f->wrcfga_count == f->fail_wrcfga_at)) {
            return AMS_ADBMS_RESULT_TRANSPORT_IO;
        }
    }
    if ((command == AMS_ADBMS_CMD_UNSNAP) && (f->fail_unsnap_count > 0U)) {
        f->fail_unsnap_count--;
        return AMS_ADBMS_RESULT_TRANSPORT_IO;
    }

    if (command == AMS_ADBMS_CMD_STCOMM) {
        static const uint8_t golden[13]={0x07,0x23,0xB9,0xE4,0,0,0,0,0,0,0,0,0};
        assert(tx_len==13U && memcmp(tx,golden,13U)==0);
        assert(f->comm[0]==0x68 && f->comm[2]==0x08 && f->comm[4]==0x19 && f->comm[5]==0xff);
        unsigned mux=(f->comm[1]>>1U)-0x4cU;
        assert(mux<3U && (f->comm[1]&1U)==0U);
        unsigned pos=0; while(pos<8 && f->comm[3]!=(1U<<pos)) ++pos;
        assert(pos<8); f->selected[mux]=(uint8_t)pos;
        f->comm_started=true; ++f->stcomm_count;
    }
    if(command>=AMS_ADBMS_CMD_ADAX_GPIO1 && command<=AMS_ADBMS_CMD_ADAX_OW_UP_GPIO3)
        f->last_conversion=command;
    if (info.kind == AMS_ADBMS_COMMAND_WRITE6) {
        assert(tx_len == 12U);
        assert(ams_adbms_pec10(&tx[4], 0U) ==
               (uint16_t)(((uint16_t)tx[10] << 8U) | tx[11]));
        if (command == AMS_ADBMS_CMD_WRCOMM) {memcpy(f->comm,&tx[4],6U);f->comm_started=false;}
        if (command == AMS_ADBMS_CMD_WRCFGA) memcpy(f->cfga, &tx[4], 6U);
        if (command == AMS_ADBMS_CMD_WRCFGB) memcpy(f->cfgb, &tx[4], 6U);
        if (command == AMS_ADBMS_CMD_WRPWMA) { memcpy(f->pwma, &tx[4], 6U); f->wrpwma_count++; }
        if (command == AMS_ADBMS_CMD_WRPWMB) { memcpy(f->pwmb, &tx[4], 6U); f->wrpwmb_count++; }
    }

    if (info.counter_effect == AMS_ADBMS_COUNTER_RESET) {f->counter = 0U;f->muted=false;f->snapshot=false;}
    else if (info.counter_effect == AMS_ADBMS_COUNTER_INCREMENT) increment_counter(f);

    if (command == AMS_ADBMS_CMD_ADCV_Z017) {
        f->adcv_count++;
        f->ccts = 100U;
    }
    if (command == AMS_ADBMS_CMD_SNAP) { f->snapshot = true; f->snap_count++; }
    if (command == AMS_ADBMS_CMD_UNSNAP) { f->snapshot = false; f->unsnap_count++; }
    if (command == AMS_ADBMS_CMD_MUTE) { f->muted = true; f->mute_count++; }
    if (f->clock_fail_after_write_command_once == command) {
        f->clock_fail_after_write_command_once = -1;
        f->clock_fail_pending = true;
    }
    if (f->arm_cleanup_wake_fail_after_command_once == command) {
        f->arm_cleanup_wake_fail_after_command_once = -1;
        f->wake_fail_count++;
    }
    return AMS_ADBMS_RESULT_OK;
}

static void fill_status_c(fake_t *f, int command, uint8_t data[6])
{
    memset(data, 0, 6U);
    if (command == AMS_ADBMS_CMD_RDSTATCERR) {
        data[5] |= 0x10U;
    } else {
        uint8_t flag = f->cfga[1];
        switch (flag) {
        case 0x01U:
        case 0x02U:
            f->post_attempt_osc_fast++;
            if (f->post_fail_osc_fast_count != 0U) {
                f->post_fail_osc_fast_count--;
            } else {
                data[5] |= 0x01U;
            }
            break;
        case 0x04U: data[4] |= 0x40U; break;
        case 0x0CU: data[4] |= 0x80U; break;
        case 0x10U: data[5] |= 0x04U; break;
        case 0x20U: data[4] |= 0x0AU; break;
        case 0x40U: data[4] |= 0x05U; break;
        case 0x80U: data[5] |= 0x02U; break;
        default: break;
        }
        if ((f->post_unexpected_flag_once != 0U) &&
            (flag == f->post_unexpected_flag_once)) {
            data[5] |= 0x02U; /* TMODCHK is unexpected during oscillator test. */
            f->post_unexpected_flag_once = 0U;
        }
    }
    uint16_t ccts = f->ccts_zero_count != 0U ? 0U : f->ccts;
    if (f->ccts_zero_count != 0U) f->ccts_zero_count--;
    uint16_t ct = (uint16_t)(ccts >> 2U);
    data[2] |= (uint8_t)((ct >> 6U) & 0x1FU);
    data[3] |= (uint8_t)((ct << 2U) & 0xFCU);
    data[3] |= (uint8_t)(ccts & 0x03U);
}

static void fill_cell_group(int command, uint8_t data[6])
{
    int group = -1;
    bool avg = false;
    bool iir = false;
    const ams_adbms_command_t raw[6] = {AMS_ADBMS_CMD_RDCVA,AMS_ADBMS_CMD_RDCVB,AMS_ADBMS_CMD_RDCVC,AMS_ADBMS_CMD_RDCVD,AMS_ADBMS_CMD_RDCVE,AMS_ADBMS_CMD_RDCVF};
    const ams_adbms_command_t av[6] = {AMS_ADBMS_CMD_RDACA,AMS_ADBMS_CMD_RDACB,AMS_ADBMS_CMD_RDACC,AMS_ADBMS_CMD_RDACD,AMS_ADBMS_CMD_RDACE,AMS_ADBMS_CMD_RDACF};
    const ams_adbms_command_t fi[6] = {AMS_ADBMS_CMD_RDFCA,AMS_ADBMS_CMD_RDFCB,AMS_ADBMS_CMD_RDFCC,AMS_ADBMS_CMD_RDFCD,AMS_ADBMS_CMD_RDFCE,AMS_ADBMS_CMD_RDFCF};
    group = -1;
    for (int g = 0; g < 6; ++g) {
        if (command == (int)raw[g]) { group = g; }
        if (command == (int)av[g])  { group = g; avg = true; }
        if (command == (int)fi[g])  { group = g; iir = true; }
    }
    assert(group >= 0);
    memset(data,0,6U);
    int first = group * 3;
    int count = group < 5 ? 3 : 1;
    for (int i=0;i<count;i++) {
        int cell = first + i;
        int mv = 3600 + cell * 10 + (avg ? 2 : 0) + (iir ? 4 : 0);
        put16(&data[i*2], cell_mv_code(mv));
    }
}

static ams_adbms_result_t fake_read(void *ctx, const uint8_t *tx, uint16_t tx_len,
                                    uint8_t *rx, uint16_t rx_len)
{
    fake_t *f = ctx;
    int command = command_from_frame(tx, tx_len);
    ams_adbms_result_t fail;
    uint8_t data[6] = {0};
    assert(command >= 0);
    assert(tx_len == 4U && rx_len == 8U);
    assert(validate_command_pec(tx));
    f->read_count++;
    f->now_us += 160U;
    if ((f->fail_read_call != 0U) && (f->read_count == f->fail_read_call)) {
        return AMS_ADBMS_RESULT_TRANSPORT_IO;
    }

    fail = maybe_transport_fail(f, command);
    if (fail != AMS_ADBMS_RESULT_OK) return fail;

    switch (command) {
    case AMS_ADBMS_CMD_RDCOMM:
        memcpy(data,f->comm,6U);
        if(f->comm_started) {data[0]=f->ack_bad?0x6f:0x67;data[2]=(f->stcomm_count&1)?0x77:0x07;}
        else if(f->pre_comm_bad) data[3]^=1U;
        break;
    case AMS_ADBMS_CMD_RDAUXA:
    case AMS_ADBMS_CMD_RDRAXA:
        assert(!f->snapshot);
        for(unsigned mux=0;mux<3; ++mux) {
            int16_t raw;
            assert(thermistor_adbms_raw_from_temperature_c(20.0f+(float)(mux*8+f->selected[mux]),5.0f,&raw));
            if(command==AMS_ADBMS_CMD_RDRAXA) raw=(int16_t)(raw+f->aux2_offset);
            if(!f->ow_no_response && f->last_conversion>=AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO1 && f->last_conversion<=AMS_ADBMS_CMD_ADAX_OW_DOWN_GPIO3) raw-=100;
            if(!f->ow_no_response && f->last_conversion>=AMS_ADBMS_CMD_ADAX_OW_UP_GPIO1 && f->last_conversion<=AMS_ADBMS_CMD_ADAX_OW_UP_GPIO3) raw+=100;
            put16(&data[mux*2],raw);
        }
        break;
    case AMS_ADBMS_CMD_RDSID:
        data[1] = f->wrong_sid ? 0x00U : 0x06U;
        break;
    case AMS_ADBMS_CMD_RDCFGA:
        memcpy(data, f->cfga, 6U);
        if (f->muted) data[5] |= 0x10U;
        if (f->mute_readback_bad) data[5] &= (uint8_t)~0x10U;
        if (f->config_readback_bad) data[5] ^= 0x01U;
        break;
    case AMS_ADBMS_CMD_RDCFGB:
        memcpy(data, f->cfgb, 6U);
        if (f->config_readback_bad) data[0] ^= 0x01U;
        break;
    case AMS_ADBMS_CMD_RDPWMA:
        memcpy(data, f->pwma, 6U);
        if (f->pwma_readback_bad) data[0] ^= 0x01U;
        break;
    case AMS_ADBMS_CMD_RDPWMB:
        memcpy(data, f->pwmb, 6U);
        if (f->pwmb_readback_bad) data[0] ^= 0x01U;
        break;
    case AMS_ADBMS_CMD_RDSTATA:
        put16(&data[0], mv_status_code(3000));
        put16(&data[2], temp_code(250));
        put16(&data[4], mv_status_code(3000));
        break;
    case AMS_ADBMS_CMD_RDSTATB:
        put16(&data[0], mv_status_code(3000));
        put16(&data[2], mv_status_code(5000));
        put16(&data[4], mv_status_code(3000));
        break;
    case AMS_ADBMS_CMD_RDSTATC:
    case AMS_ADBMS_CMD_RDSTATCERR:
        fill_status_c(f, command, data);
        break;
    case AMS_ADBMS_CMD_RDSTATD:
        data[5] = 60U;
        break;
    case AMS_ADBMS_CMD_RDSTATE:
        break;
    default:
        fill_cell_group(command, data);
        break;
    }

    if (f->override_command_once == command) {
        assert(f->override_slot < 3U);
        put16(&data[f->override_slot * 2U], f->override_code);
        f->override_command_once = -1;
    }

    memcpy(rx, data, 6U);
    uint16_t pec = ams_adbms_pec10(data, f->counter);
    rx[6] = (uint8_t)((f->counter << 2U) | ((pec >> 8U) & 0x03U));
    rx[7] = (uint8_t)pec;
    if (f->corrupt_command_once == command) {
        f->corrupt_command_once = -1;
        rx[0] ^= 1U;
    }
    if (f->gap_after_command_once == command) {
        f->gap_after_command_once = -1;
        /* session_read() samples time once immediately after transport to mark
         * activity. Inject the idle gap only on the following time sample so
         * the next command must detect an expired 3 ms session. */
        f->gap_pending = true;
        f->gap_skip_mark_activity = true;
    }
    return AMS_ADBMS_RESULT_OK;
}

static ams_adbms_monitor_io_t fake_io(fake_t *f)
{
    ams_adbms_monitor_io_t io = {
        .context=f,.now_us=fake_now,.delay_us=fake_delay,.wake_b=fake_wake,
        .write_b=fake_write,.write_read_b=fake_read
    };
    return io;
}

static void test_protocol(void)
{
    uint8_t data[6], frame[12];
    ams_adbms_counter_tracker_t t;
    ams_adbms_command_info_t info;
    ams_adbms_z017_production_cfga(data);
    const uint8_t cfga[6]={0x81,0,0,0xff,0x03,0x03};
    assert(memcmp(data,cfga,6)==0);
    ams_adbms_z017_production_cfgb(data);
    const uint8_t cfgb[6]={0x71,0x52,0x46,0,0,0};
    assert(memcmp(data,cfgb,6)==0);
    assert(ams_adbms_build_write_frame(AMS_ADBMS_CMD_WRCFGA,cfga,frame));
    assert(frame[0]==0 && frame[1]==1);
    assert((((uint16_t)frame[2]<<8)|frame[3])==ams_adbms_pec15(frame,2));
    assert((((uint16_t)frame[10]<<8)|frame[11])==ams_adbms_pec10(cfga,0));
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_ADCV_Z017,&info));
    assert(info.byte0==0x03 && info.byte1==0xe0 && info.counter_effect==AMS_ADBMS_COUNTER_INCREMENT);
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_MUTE,&info));
    assert(info.byte0==0x00 && info.byte1==0x28 && info.counter_effect==AMS_ADBMS_COUNTER_INCREMENT);
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_WRPWMA,&info) && info.byte1==0x20 && info.kind==AMS_ADBMS_COMMAND_WRITE6);
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_WRPWMB,&info) && info.byte1==0x21 && info.kind==AMS_ADBMS_COMMAND_WRITE6);
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_RDPWMA,&info) && info.byte1==0x22 && info.kind==AMS_ADBMS_COMMAND_READ);
    assert(ams_adbms_command_info(AMS_ADBMS_CMD_RDPWMB,&info) && info.byte1==0x23 && info.kind==AMS_ADBMS_COMMAND_READ);
    for (unsigned c=0;c<(unsigned)AMS_ADBMS_CMD_COUNT;c++) {
        assert(ams_adbms_command_info((ams_adbms_command_t)c,&info));
        assert(!(info.byte0==0x00U && info.byte1==0x29U)); /* UNMUTE is not admitted. */
    }
    for(unsigned n=0;n<64;n++) {
        assert(ams_adbms_counter_next((uint8_t)n)==(n==0||n==63?1:n+1));
    }
    ams_adbms_counter_reset(&t);
    assert(t.known && t.expected==0);
    for(unsigned n=0;n<70;n++) {
        uint8_t e=ams_adbms_counter_next(t.expected);
        ams_adbms_counter_note_success(&t,AMS_ADBMS_COUNTER_INCREMENT);
        assert(t.expected==e);
    }
    ams_adbms_counter_unknown(&t);
    assert(!t.known);
    assert(ams_adbms_counter_observe(&t,37,true)==AMS_ADBMS_RESULT_OK && t.expected==37);
    assert(ams_adbms_counter_observe(&t,0,true)==AMS_ADBMS_RESULT_COUNTER);
    assert(t.unexpected_reset_count==1);

    /* Signed/raw-code conversion boundaries and all frozen invalid sentinels. */
    {
        uint16_t mv=0U;
        assert(!ams_adbms_cell_code_to_mv(INT16_MIN,&mv));
        assert(!ams_adbms_cell_code_to_mv(INT16_MAX,&mv));
        assert(!ams_adbms_cell_code_to_mv((int16_t)-1,&mv));
        assert(!ams_adbms_cell_code_to_mv((int16_t)-10001,&mv));
        assert(ams_adbms_cell_code_to_mv((int16_t)-10000,&mv) && mv==0U);
    }
}

static void test_cell_image(void)
{
    ams_cell_image_t image;
    int16_t codes[16];
    for(int i=0;i<16;i++) codes[i]=cell_mv_code(3700+i);
    ams_cell_image_init(&image);
    ams_cell_image_apply_raw(&image,codes,0xffff,0,1000);
    assert(image.updated_mask==0x7fff && image.usable_mask==0x7fff && image.usable_count==15);
    assert(image.sum_mv>55000 && image.raw_mv[0]==3700);
    /* Cell 16 is intentionally not a monitored-cell requirement. */
    assert((image.updated_mask & 0x8000U)==0U);

    ams_cell_image_apply_raw(&image,codes,0,0,3499);
    assert(image.usable_mask==0x7fff);
    assert(image.consecutive_misses[0]==1);
    ams_cell_image_apply_raw(&image,codes,0,0,3500);
    assert(image.usable_mask==0x7fff && image.consecutive_misses[0]==2);
    ams_cell_image_apply_raw(&image,codes,0,0,3501);
    assert(image.usable_mask==0 && image.consecutive_misses[0]==3);

    ams_cell_image_init(&image);
    ams_cell_image_apply_raw(&image,codes,0x7fff,0,UINT32_MAX-1000U);
    ams_cell_image_apply_raw(&image,codes,0,0,500U);
    assert(image.usable_mask==0x7fff); /* wrap-safe age 1501 ms, miss 1 */

    ams_cell_image_init(&image);
    ams_cell_image_apply_raw(&image,codes,0x7fff,0,100);
    codes[0]=cell_mv_code(3950);
    ams_cell_image_apply_raw(&image,codes,0x7fff,0,200);
    assert(image.jump_mask&1U);
    codes[0]=(int16_t)0x8000;
    ams_cell_image_apply_raw(&image,codes,0x7fff,0,300);
    assert((image.updated_mask&1U)==0U && image.consecutive_misses[0]==1);
    codes[0]=(int16_t)0xffff;
    ams_cell_image_apply_raw(&image,codes,0x7fff,0,400);
    assert((image.updated_mask&1U)==0U && image.consecutive_misses[0]==2);

    codes[0]=cell_mv_code(3800);
    ams_cell_image_init(&image);
    ams_cell_image_apply_raw(&image,codes,1,0,0);
    for(unsigned i=0;i<120;i++) ams_cell_image_apply_raw(&image,codes,1,0,i+1);
    assert(image.stuck_mask&1U);

    /* Exact plausibility boundaries: 500 and 5000 mV are admitted; values
     * immediately outside the band are not allowed to refresh history. */
    ams_cell_image_init(&image);
    for(int i=0;i<16;i++) codes[i]=cell_mv_code(3700+i);
    codes[0]=cell_mv_code(500);
    codes[1]=cell_mv_code(5000);
    codes[2]=cell_mv_code(499);
    codes[3]=cell_mv_code(5001);
    ams_cell_image_apply_raw(&image,codes,0x000fU,0U,100U);
    assert((image.updated_mask & 0x0003U)==0x0003U);
    assert((image.updated_mask & 0x000cU)==0U);
    assert(image.raw_mv[0]>=500U && image.raw_mv[1]<=5000U);

    /* Unchanged-count evidence saturates; it must never wrap and temporarily
     * clear the stuck indication after long stationary operation. */
    ams_cell_image_init(&image);
    for(int i=0;i<16;i++) codes[i]=cell_mv_code(3800+i);
    ams_cell_image_apply_raw(&image,codes,1U,0U,0U);
    for(unsigned i=0;i<400U;i++) {
        ams_cell_image_apply_raw(&image,codes,1U,0U,i+1U);
    }
    assert(image.same_count[0]==UINT8_MAX && (image.stuck_mask&1U)!=0U);

    ams_cell_image_init(&image);
    for(int i=0;i<16;i++) codes[i]=cell_mv_code(3700+i);
    ams_cell_image_apply_iir(&image,codes,0xffff,0,1000,true);
    assert(!image.iir_ready && image.filtered_successful_epoch_count==1 && image.iir_usable_mask==0U);
    ams_cell_image_apply_iir(&image,codes,0xffff,0,1099,true);
    assert(!image.iir_ready && image.filtered_successful_epoch_count==1 && image.iir_usable_mask==0U);
    ams_cell_image_apply_iir(&image,codes,0xffff,0,1199,true);
    assert(image.iir_ready && image.filtered_successful_epoch_count==2 && image.iir_usable_mask==0x7fffU);
    ams_cell_image_invalidate_iir(&image);
    assert(!image.iir_ready && image.filtered_successful_epoch_count==0);
}

static void test_monitor_success(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io; ams_adbms_monitor_snapshot_t s;
    fake_init(&f); io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    assert(m.state==AMS_ADBMS_MONITOR_READY && m.config_verified && m.startup_post_passed);
    assert(m.balance_mute_verified && m.balance_durable_zero_verified && m.balance_inhibit_attempt_count==1U);
    assert(f.muted && f.mute_count==1U && f.wrpwma_count>=1U && f.wrpwmb_count>=1U);
    assert(m.post_attempts==1 && f.cfga[1]==0);
    unsigned adcv=f.adcv_count;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(f.adcv_count==adcv+1 && m.epoch_attempts==1);
    assert(m.raw_fresh_mask==0xffff && m.cells.updated_mask==0x7fff && m.cells.usable_mask==0x7fff);
    assert(m.ccts_valid && m.ccts!=0 && m.statd_valid);
    assert(m.cells.avg8_usable_mask==0x7fff);
    assert(!m.cells.iir_ready && m.cells.iir_usable_mask==0U);
    assert(ams_adbms_monitor_acquire(&m,&io,1100)==AMS_ADBMS_RESULT_OK);
    assert(m.cells.iir_ready && m.cells.iir_usable_mask==0x7fffU);
    ams_adbms_monitor_snapshot(&m,&s);
    assert(s.cells.usable_mask==0x7fff && !s.physical_validated && s.acquisition_live);
}

static void test_init_failures(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
    fake_init(&f); f.wrong_sid=true; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_IDENTITY);
    assert(m.state==AMS_ADBMS_MONITOR_FAULTED && !m.acquisition_live);

    fake_init(&f); f.config_readback_bad=true; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_CONFIG_MISMATCH);
    assert(m.config_mismatch_count>0);

    fake_init(&f); f.post_fail_osc_fast_count=1; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    assert(m.post_attempts==2 && f.cfga[1]==0);

    fake_init(&f); f.post_fail_osc_fast_count=2; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)!=AMS_ADBMS_RESULT_OK);
    assert(m.post_attempts==2 && f.cfga[1]==0 && !m.startup_post_passed);

    fake_init(&f); f.post_unexpected_flag_once=0x01U; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    assert(m.post_attempts==2 && (m.post_failed_stage_mask&1U)!=0U &&
           (m.post_unexpected_stage_mask&1U)!=0U);

    /* Initial production WRCFGA is #1; POST production restore after a forced
     * first-stage failure is #4. A failed mandatory restoration is terminal
     * for startup and must not be hidden by a second POST attempt. */
    fake_init(&f); f.post_fail_osc_fast_count=1; f.fail_wrcfga_at=4U; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_TRANSPORT_IO);
    assert(m.post_attempts==1 && !m.config_verified && !m.startup_post_passed);
}

static int write_trace_find_after(const fake_t *f, int command, unsigned start)
{
    for (unsigned i=start; i<f->write_trace_count; ++i) {
        if (f->write_trace[i] == command) return (int)i;
    }
    return -1;
}

static void test_balance_inhibit_failures(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;

    fake_init(&f); f.transport_fail_command_once=AMS_ADBMS_CMD_MUTE;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_TRANSPORT_IO);
    assert(!m.balance_mute_verified && m.balance_durable_zero_verified);
    assert(m.balance_inhibit_attempt_count==1U && m.balance_inhibit_fail_count==1U);
    assert(f.wrpwma_count>=1U && f.wrpwmb_count>=1U);

    fake_init(&f); f.mute_readback_bad=true; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_BALANCE_INHIBIT);
    assert(!m.balance_mute_verified && m.balance_durable_zero_verified);

    fake_init(&f); f.transport_fail_command_once=AMS_ADBMS_CMD_WRPWMA;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_TRANSPORT_IO);
    assert(m.balance_mute_verified && !m.balance_durable_zero_verified);
    /* Best-effort rewrite still leaves both emulated banks at zero. */
    for (unsigned i=0;i<6U;i++) assert(f.pwma[i]==0U && f.pwmb[i]==0U);

    fake_init(&f); f.pwma_readback_bad=true; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_BALANCE_INHIBIT);
    assert(m.balance_mute_verified && !m.balance_durable_zero_verified);

    fake_init(&f); f.transport_fail_command_once=AMS_ADBMS_CMD_RDPWMB;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO; io=fake_io(&f);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_TRANSPORT_IO);
    assert(m.balance_mute_verified && !m.balance_durable_zero_verified);
}

static void assert_safe_init_outcome(const ams_adbms_monitor_t *m,
                                     ams_adbms_result_t result)
{
    assert(m->post_attempts <= AMS_ADBMS_Z017_MAX_POST_ATTEMPTS);
    if (result == AMS_ADBMS_RESULT_OK) {
        assert(m->state == AMS_ADBMS_MONITOR_READY);
        assert(m->initialized && m->acquisition_live);
        assert(m->config_verified && m->startup_post_passed);
        assert(m->balance_mute_verified && m->balance_durable_zero_verified);
    } else {
        assert(m->state == AMS_ADBMS_MONITOR_FAULTED);
        assert(!m->initialized && !m->acquisition_live);
    }
}

static void test_every_startup_transfer_fault(void)
{
    fake_t baseline; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
    unsigned writes, reads;

    fake_init(&baseline); io=fake_io(&baseline);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    writes=baseline.write_count; reads=baseline.read_count;
    assert(writes>0U && reads>0U);

    for(unsigned call=1U; call<=writes; ++call) {
        fake_t f; ams_adbms_result_t r;
        fake_init(&f); f.fail_write_call=call; io=fake_io(&f);
        r=ams_adbms_monitor_initialize(&m,&io);
        assert_safe_init_outcome(&m,r);
        assert(m.post_attempts<=AMS_ADBMS_Z017_MAX_POST_ATTEMPTS);
    }
    for(unsigned call=1U; call<=reads; ++call) {
        fake_t f; ams_adbms_result_t r;
        fake_init(&f); f.fail_read_call=call; io=fake_io(&f);
        r=ams_adbms_monitor_initialize(&m,&io);
        assert_safe_init_outcome(&m,r);
        assert(m.post_attempts<=AMS_ADBMS_Z017_MAX_POST_ATTEMPTS);
    }
}

static void test_every_acquisition_read_fault(void)
{
    fake_t baseline; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
    unsigned base_reads, reads_per_scan;

    fake_init(&baseline); io=fake_io(&baseline);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    base_reads=baseline.read_count;
    assert(ams_adbms_monitor_acquire(&m,&io,1000U)==AMS_ADBMS_RESULT_OK);
    reads_per_scan=baseline.read_count-base_reads;
    assert(reads_per_scan>0U);

    for(unsigned offset=1U; offset<=reads_per_scan; ++offset) {
        fake_t f; ams_adbms_result_t r; unsigned adcv_before;
        fake_init(&f); io=fake_io(&f);
        assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
        f.fail_read_call=f.read_count+offset;
        adcv_before=f.adcv_count;
        r=ams_adbms_monitor_acquire(&m,&io,1000U);
        /* Optional product failures may degrade the product only; mandatory or
         * session-integrity failures may consume the one whole-epoch retry. */
        assert(r==AMS_ADBMS_RESULT_OK || m.epoch_attempts<=AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS);
        assert(f.adcv_count==adcv_before+1U);
        assert(!m.snapshot_cleanup_required);
        if (r==AMS_ADBMS_RESULT_OK) {
            assert(m.cells.usable_mask==AMS_CELL_IMAGE_MONITORED_MASK);
        } else {
            assert(m.scan_fail_count>0U);
        }
    }
}

static void test_epoch_expiry_across_mandatory_groups(void)
{
    static const ams_adbms_command_t commands[] = {
        AMS_ADBMS_CMD_RDCVA, AMS_ADBMS_CMD_RDCVB, AMS_ADBMS_CMD_RDCVC,
        AMS_ADBMS_CMD_RDCVD, AMS_ADBMS_CMD_RDCVE, AMS_ADBMS_CMD_RDCVF,
        AMS_ADBMS_CMD_RDSTATC, AMS_ADBMS_CMD_RDSTATD,
    };
    for(unsigned i=0U;i<sizeof(commands)/sizeof(commands[0]);++i) {
        fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io; unsigned adcv;
        fake_init(&f); io=fake_io(&f);
        assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
        f.gap_after_command_once=commands[i]; f.gap_us=AMS_ADBMS_Z017_SESSION_GUARD_US;
        adcv=f.adcv_count;
        assert(ams_adbms_monitor_acquire(&m,&io,1000U)==AMS_ADBMS_RESULT_OK);
        assert(m.epoch_attempts<=AMS_ADBMS_Z017_MAX_EPOCH_ATTEMPTS);
        assert(f.adcv_count==adcv+1U);
        assert(!m.snapshot_cleanup_required);
    }
}

static void test_snapshot_ownership_faults(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
    int snap1, unsnap1, snap2;

    /* Uncertain SNAP return may still mean the remote monitor snapped. Cleanup
     * must be owed before any whole-epoch retry. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.transport_fail_command_once=AMS_ADBMS_CMD_SNAP;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO;
    f.transport_fail_applies_remote=true;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(!f.snapshot && !m.snapshot_cleanup_required && m.epoch_attempts==2U);
    snap1=write_trace_find_after(&f,AMS_ADBMS_CMD_SNAP,0U);
    unsnap1=write_trace_find_after(&f,AMS_ADBMS_CMD_UNSNAP,(unsigned)(snap1+1));
    snap2=write_trace_find_after(&f,AMS_ADBMS_CMD_SNAP,(unsigned)(unsnap1+1));
    assert(snap1>=0 && unsnap1>snap1 && snap2>unsnap1);

    /* SNAP reaches the remote device, then the local clock source fails while
     * marking activity. Cleanup obligation must survive the lost session. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.clock_fail_after_write_command_once=AMS_ADBMS_CMD_SNAP;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(!f.snapshot && !m.snapshot_cleanup_required && m.epoch_attempts==2U);

    /* Even when cleanup-only re-wake itself fails once, the second epoch may
     * not issue another SNAP until a subsequent UNSNAP cleanup succeeds. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.clock_fail_after_write_command_once=AMS_ADBMS_CMD_SNAP;
    f.arm_cleanup_wake_fail_after_command_once=AMS_ADBMS_CMD_SNAP;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    snap1=write_trace_find_after(&f,AMS_ADBMS_CMD_SNAP,0U);
    unsnap1=write_trace_find_after(&f,AMS_ADBMS_CMD_UNSNAP,(unsigned)(snap1+1));
    snap2=write_trace_find_after(&f,AMS_ADBMS_CMD_SNAP,(unsigned)(unsnap1+1));
    assert(snap1>=0 && unsnap1>snap1 && snap2>unsnap1);

    /* An uncertain UNSNAP is not proof of cleanup even if the remote happened
     * to apply it; retry must positively complete UNSNAP before a new SNAP. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.transport_fail_command_once=AMS_ADBMS_CMD_UNSNAP;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO;
    f.transport_fail_applies_remote=true;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(!f.snapshot && !m.snapshot_cleanup_required && m.epoch_attempts==2U);
    assert(f.unsnap_count>=3U);

    /* If cleanup cannot be proven across both allowed epoch attempts, do not
     * begin a second snapshot. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.fail_unsnap_count=2U;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)!=AMS_ADBMS_RESULT_OK);
    assert(f.snap_count==1U && m.snapshot_cleanup_required);
}

static void test_epoch_faults(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io; unsigned adcv;

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    adcv=f.adcv_count; f.corrupt_command_once=AMS_ADBMS_CMD_RDCVC;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==2 && m.coherent_restart_count==1 && f.adcv_count==adcv+1);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    adcv=f.adcv_count; f.ccts_zero_count=1;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==2 && f.adcv_count==adcv+1 && m.ccts_fault_count==1);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.corrupt_command_once=AMS_ADBMS_CMD_RDACA;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==1 && m.cells.usable_mask==0x7fff && m.cells.avg8_usable_mask==0);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.corrupt_command_once=AMS_ADBMS_CMD_RDFCB;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.cells.usable_mask==0x7fff && !m.cells.iir_ready && m.filtered_fail_count==1);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.fail_unsnap_count=1; adcv=f.adcv_count;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==2 && f.adcv_count==adcv+1);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.gap_after_command_once=AMS_ADBMS_CMD_RDCVB; f.gap_us=3000;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==2 && m.session_expiry_count>0);

    /* A recovered transport failure after SNAP must not forget remote cleanup.
     * Cleanup re-wakes/UNSNAPs before the complete epoch retry; ADCV is not
     * reissued. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.transport_fail_command_once=AMS_ADBMS_CMD_RDCVB;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO;
    adcv=f.adcv_count; unsigned unsnap_before=f.unsnap_count;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert(m.epoch_attempts==2 && f.adcv_count==adcv+1);
    assert(f.unsnap_count>=unsnap_before+2U && !m.snapshot_cleanup_required);

    /* Invalid result sentinels/data do not refresh that cell, but a coherent
     * epoch can still succeed with explicit bad/update masks. */
    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.override_command_once=AMS_ADBMS_CMD_RDCVA; f.override_slot=0U; f.override_code=INT16_MIN;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_OK);
    assert((m.raw_bad_mask&1U)!=0U && (m.cells.updated_mask&1U)==0U && (m.cells.usable_mask&1U)==0U);

    fake_init(&f); io=fake_io(&f); assert(ams_adbms_monitor_initialize(&m,&io)==0);
    f.transport_fail_command_once=AMS_ADBMS_CMD_RDCVA;
    f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_TERMINAL;
    assert(ams_adbms_monitor_acquire(&m,&io,1000)==AMS_ADBMS_RESULT_TRANSPORT_TERMINAL);
    assert(m.epoch_attempts==1);
}

static void randomized_cell_history(void)
{
    uint32_t x=0x12345678U;
    ams_cell_image_t image;
    int16_t codes[16];
    ams_cell_image_init(&image);
    uint32_t now=0;
    for(unsigned n=0;n<50000;n++) {
        uint16_t fresh=0,bad=0;
        for(unsigned c=0;c<16;c++) {
            x=x*1664525U+1013904223U;
            int mv=500+(int)((x>>16)%4501U);
            codes[c]=cell_mv_code(mv);
            if(x&1U) fresh|=(uint16_t)(1U<<c);
            if(x&2U) bad|=(uint16_t)(1U<<c);
        }
        x=x*1664525U+1013904223U;
        now+=(x%150U);
        ams_cell_image_apply_raw(&image,codes,fresh,bad,now);
        assert((image.updated_mask & ~0x7fffU)==0U);
        assert((image.usable_mask & ~0x7fffU)==0U);
        assert((image.stale_mask & ~0x7fffU)==0U);
        assert((image.usable_mask & image.stale_mask)==0U);
        assert((uint16_t)(image.usable_mask | image.stale_mask)==0x7fffU);
    }
}

static void test_z018(void);
static void test_z019(void);

int main(void)
{
    test_z019();
    test_z018();
    test_protocol();
    test_cell_image();
    test_monitor_success();
    test_init_failures();
    test_balance_inhibit_failures();
    test_every_startup_transfer_fault();
    test_every_acquisition_read_fault();
    test_epoch_expiry_across_mandatory_groups();
    test_snapshot_ownership_faults();
    test_epoch_faults();
    randomized_cell_history();
    puts("PASS Z017 monitor: startup/POST/config/balance-inhibit/counter/snapshot ownership/exhaustive transfer-fault/epoch expiry/products/history boundaries; 50000 randomized cell-history steps");
    return 0;
}

static void test_z018(void)
{
 fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
 fake_init(&f);io=fake_io(&f);ams_adbms_monitor_reset(&m);
 assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
 for(unsigned release=0;release<8;release++) {
  assert(ams_adbms_monitor_temperature(&m,&io,release*100U)==AMS_ADBMS_RESULT_OK);
  assert(m.temperature.image.fresh_mask==((1UL<<release)|(1UL<<(release+8))|(1UL<<(release+16))));
 }
 assert(m.temperature.image.usable_mask==AMS_TEMP_ALL_MASK && m.temperature.image.startup_scan_complete);
 for(unsigned i=0;i<24;i++) assert(m.temperature.image.deci_c[i]==(int16_t)((20+i)*10));
 assert(f.stcomm_count==24);
 ams_temp_image_t saved=m.temperature.image;
 assert(ams_adbms_monitor_aux2(&m,&io,2)==AMS_ADBMS_RESULT_OK);
 assert(m.temperature.aux2.valid && !m.temperature.aux2.suspect);
 f.aux2_offset=137;
 assert(ams_adbms_monitor_aux2(&m,&io,2)==AMS_ADBMS_RESULT_DIAGNOSTIC);
 assert(m.temperature.aux2.suspect);
 f.aux2_offset=0;
 assert(ams_adbms_monitor_therm_ow(&m,&io,2)==AMS_ADBMS_RESULT_OK);
 assert(m.temperature.open_wire.valid && !m.temperature.open_wire.suspect);
 assert(!m.temperature.config_cleanup_required && m.config_verified);
 assert(memcmp(&saved,&m.temperature.image,sizeof(saved))==0);
 f.ow_no_response=true;
 assert(ams_adbms_monitor_therm_ow(&m,&io,2)==AMS_ADBMS_RESULT_DIAGNOSTIC);
 f.ow_no_response=false;
 // Remote accepts temporary CFGA before reporting uncertainty; restore is owed.
 f.transport_fail_command_once=AMS_ADBMS_CMD_WRCFGA;
 f.transport_fail_result=AMS_ADBMS_RESULT_TRANSPORT_IO;f.transport_fail_applies_remote=true;
 assert(ams_adbms_monitor_therm_ow(&m,&io,2)==AMS_ADBMS_RESULT_TRANSPORT_IO);
 assert(f.cfga[2]==0 && !m.temperature.config_cleanup_required && m.config_verified);
 // Uncertain STCOMM invalidates all routing beliefs and does not retry.
 unsigned st=f.stcomm_count;
 f.transport_fail_command_once=AMS_ADBMS_CMD_STCOMM;f.transport_fail_applies_remote=true;
 assert(ams_adbms_monitor_temperature(&m,&io,900)==AMS_ADBMS_RESULT_TRANSPORT_IO);
 assert(f.stcomm_count==st+1 && m.temperature.mux_valid_mask==0 && m.temperature.image.fresh_mask==0);
 f.ack_bad=true;st=f.stcomm_count;
 assert(ams_adbms_monitor_temperature(&m,&io,1000)==AMS_ADBMS_RESULT_DIAGNOSTIC);
 assert(f.stcomm_count==st+1 && m.temperature.image.fresh_mask==0);f.ack_bad=false;
 f.pre_comm_bad=true;st=f.stcomm_count;
 assert(ams_adbms_monitor_temperature(&m,&io,1100)==AMS_ADBMS_RESULT_CONFIG_MISMATCH);
 assert(f.stcomm_count==st);f.pre_comm_bad=false;
 // Failure on the second mux preserves the first timestamp and sample.
 f.fail_read_call=f.read_count+4;
 unsigned position=m.temperature.next_position;
 assert(ams_adbms_monitor_temperature(&m,&io,1200)==AMS_ADBMS_RESULT_TRANSPORT_IO);
 assert(m.temperature.image.fresh_mask==(1UL<<position)); f.fail_read_call=0;
 // Exhaustive read/write boundary failures, including restore failures.
 for(unsigned diagnostic=0;diagnostic<3;diagnostic++) {
  fake_t base; ams_adbms_monitor_t model;
  fake_init(&base);ams_adbms_monitor_reset(&model);io=fake_io(&base);
  assert(ams_adbms_monitor_initialize(&model,&io)==AMS_ADBMS_RESULT_OK);
  unsigned wr=base.write_count,rd=base.read_count;
  if(diagnostic==0) assert(ams_adbms_monitor_temperature(&model,&io,100)==AMS_ADBMS_RESULT_OK);
  if(diagnostic==1) assert(ams_adbms_monitor_aux2(&model,&io,0)==AMS_ADBMS_RESULT_OK);
  if(diagnostic==2) assert(ams_adbms_monitor_therm_ow(&model,&io,0)==AMS_ADBMS_RESULT_OK);
  unsigned nw=base.write_count-wr,nr=base.read_count-rd;
  for(unsigned k=0;k<nw+nr;k++) {
   fake_init(&f);ams_adbms_monitor_reset(&m);io=fake_io(&f);
   assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
   if(k<nw) f.fail_write_call=f.write_count+k+1;
   else f.fail_read_call=f.read_count+k-nw+1;
   ams_adbms_result_t result=diagnostic==0?ams_adbms_monitor_temperature(&m,&io,100):
       diagnostic==1?ams_adbms_monitor_aux2(&m,&io,0):ams_adbms_monitor_therm_ow(&m,&io,0);
   assert(result!=AMS_ADBMS_RESULT_OK);
   assert(!m.temperature.aux2.valid && !m.temperature.open_wire.valid);
   if(m.temperature.config_cleanup_required) {
    assert(m.state==AMS_ADBMS_MONITOR_FAULTED && !m.config_verified);
    unsigned writes=f.write_count;
    assert(ams_adbms_monitor_temperature(&m,&io,200)!=AMS_ADBMS_RESULT_OK);
    assert(ams_adbms_monitor_acquire(&m,&io,200)!=AMS_ADBMS_RESULT_OK);
    assert(f.write_count==writes);
   }
  }
 }
 // History: exact stale/miss boundaries, raw zero, sentinels, timer wrap.
 ams_temp_image_t im={0};int16_t raw[24]={0};uint32_t times[24]={0};
 assert(thermistor_adbms_raw_from_temperature_c(25,5,&raw[0]));
 times[0]=UINT32_MAX-50U;
 ams_temp_image_apply(&im,raw,1,times,times[0]);assert(im.deci_c[0]==250);
 ams_temp_image_apply(&im,raw,0,times,times[0]+12000U);assert(im.usable_mask&1U);
 ams_temp_image_apply(&im,raw,0,times,times[0]+12001U);assert(!(im.usable_mask&1U));
 memset(&im,0,sizeof(im));times[0]=0;ams_temp_image_apply(&im,raw,1,times,0);
 for(unsigned k=1;k<=10;k++){ams_temp_image_apply(&im,raw,0,times,k*100);assert(im.usable_mask&1U);}
 ams_temp_image_apply(&im,raw,0,times,1100);assert(!(im.usable_mask&1U));
 raw[0]=0;times[0]=1200;ams_temp_image_apply(&im,raw,1,times,1200);assert(im.fresh_mask==1);
 raw[0]=INT16_MIN;ams_temp_image_apply(&im,raw,1,times,1200);assert(im.invalid_mask==1 && !(im.usable_mask&1));
 uint32_t seed=145U,now_ms=0;
 for(unsigned k=0;k<50000;k++) {
  seed=seed*1664525U+1013904223U;unsigned i=seed%24U;
  now_ms+=seed%1000U;times[i]=now_ms;raw[i]=(int16_t)(seed>>16U);
  ams_temp_image_apply(&im,raw,1UL<<i,times,now_ms);
  assert(!(im.usable_mask&im.stale_mask));assert((im.usable_mask|im.stale_mask)==AMS_TEMP_ALL_MASK);
  assert(!(im.fresh_mask & ~im.valid_mask));
 }
 puts("PASS Z018 monitor: routing, COMM golden frame/ACK, partial capture, uncertainty, AUX2/OW restoration, exhaustive transfer faults, temperature history + 50000 randomized steps");
}

static void test_z019(void)
{
    fake_t f; ams_adbms_monitor_t m; ams_adbms_monitor_io_t io;
    fake_init(&f);io=fake_io(&f);ams_adbms_monitor_reset(&m);
    assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    assert(ams_adbms_monitor_acquire(&m,&io,100)==AMS_ADBMS_RESULT_OK);
    assert(ams_adbms_monitor_temperature(&m,&io,100)==AMS_ADBMS_RESULT_OK);
    assert(ams_adbms_monitor_recovery_step(&m,&io,200)==AMS_ADBMS_RESULT_OK);
    assert(m.recovery.expected_fingerprint==m.recovery.observed_fingerprint);
    f.cfgb[0]^=1;
    unsigned writes=f.write_count;
    assert(ams_adbms_monitor_recovery_step(&m,&io,300)==AMS_ADBMS_RESULT_CONFIG_MISMATCH);
    assert(f.write_count==writes && m.recovery.pending && !m.config_verified);
    assert(!m.cells.usable_mask && !m.raw_fresh_mask && !m.temperature.image.usable_mask);
    assert(ams_adbms_monitor_acquire(&m,&io,300)!=AMS_ADBMS_RESULT_OK);
    assert(ams_adbms_monitor_recovery_step(&m,&io,400)==AMS_ADBMS_RESULT_OK);
    assert(m.recovery.success_count==1 && m.recovery.generation==1 && !m.recovery.pending);
    assert(m.cells.updated_mask==0x7fff && m.cells.usable_mask==0x7fff);
    assert(!m.temperature.image.usable_mask && !m.cells.iir_ready);
    assert(m.init_attempt_count==2 && m.recovery.reason==AMS_ADBMS_RESULT_CONFIG_MISMATCH);
    // Unexpected remote reset must not silently resynchronize and publish old data.
    f.counter=0;
    assert(ams_adbms_monitor_recovery_step(&m,&io,500)==AMS_ADBMS_RESULT_COUNTER);
    assert(m.recovery.pending && !m.cells.usable_mask);
    assert(ams_adbms_monitor_recovery_step(&m,&io,600)==AMS_ADBMS_RESULT_OK);
    // A successful whole-epoch retry cannot hide lost protocol continuity.
    f.corrupt_command_once=AMS_ADBMS_CMD_RDCVA;
    assert(ams_adbms_monitor_acquire(&m,&io,610)==AMS_ADBMS_RESULT_OK);
    assert(m.recovery.continuity_lost);
    writes=f.write_count;
    assert(ams_adbms_monitor_recovery_step(&m,&io,620)==AMS_ADBMS_RESULT_COUNTER);
    assert(f.write_count==writes && !m.cells.usable_mask);
    m.sticky_diag_faults|=0x80000000U;
    assert(ams_adbms_monitor_recovery_step(&m,&io,630)==AMS_ADBMS_RESULT_OK);
    assert(m.sticky_diag_faults&0x80000000U);
    // Failed cleanup is terminal and does not attempt SRST or future work.
    f.snapshot=true;m.snapshot_cleanup_required=true;
    ams_adbms_monitor_interrupt(&m,AMS_ADBMS_RESULT_TRANSPORT_IO);
    f.fail_unsnap_count=1;
    assert(ams_adbms_monitor_recovery_step(&m,&io,700)!=AMS_ADBMS_RESULT_OK);
    assert(m.recovery.terminal && m.snapshot_cleanup_required && f.snapshot);
    writes=f.write_count;
    for(unsigned i=0;i<10;i++) assert(ams_adbms_monitor_recovery_step(&m,&io,800+i)!=AMS_ADBMS_RESULT_OK);
    assert(f.write_count==writes);
    // Identity mismatch cannot trigger writes to a replacement device.
    fake_init(&f);io=fake_io(&f);assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
    f.wrong_sid=true;writes=f.write_count;
    assert(ams_adbms_monitor_recovery_step(&m,&io,10)==AMS_ADBMS_RESULT_IDENTITY);
    assert(m.recovery.terminal && f.write_count==writes);
    // Every repair read/write failure must fail closed without a second attempt.
    fake_t base;ams_adbms_monitor_t model;
    fake_init(&base);io=fake_io(&base);assert(ams_adbms_monitor_initialize(&model,&io)==AMS_ADBMS_RESULT_OK);
    ams_adbms_monitor_interrupt(&model,AMS_ADBMS_RESULT_TRANSPORT_IO);
    unsigned wr=base.write_count,rd=base.read_count;
    assert(ams_adbms_monitor_recovery_step(&model,&io,100)==AMS_ADBMS_RESULT_OK);
    unsigned nw=base.write_count-wr,nr=base.read_count-rd;
    for(unsigned k=0;k<nw+nr;k++) {
        fake_init(&f);io=fake_io(&f);assert(ams_adbms_monitor_initialize(&m,&io)==AMS_ADBMS_RESULT_OK);
        ams_adbms_monitor_interrupt(&m,AMS_ADBMS_RESULT_TRANSPORT_IO);
        if(k<nw)f.fail_write_call=f.write_count+k+1;else f.fail_read_call=f.read_count+k-nw+1;
        ams_adbms_result_t result=ams_adbms_monitor_recovery_step(&m,&io,100);
        // Existing POST/epoch bounded retries may repair an internal read failure.
        // Success still requires fresh raw cells and exact safe config.
        if(result==AMS_ADBMS_RESULT_OK) {
            assert(m.cells.updated_mask==0x7fff && m.config_verified && m.balance_durable_zero_verified);
            assert(!m.temperature.image.usable_mask && m.recovery.success_count==1);
        } else {
            assert(m.recovery.terminal && !m.cells.usable_mask && !m.raw_fresh_mask);
            writes=f.write_count;assert(ams_adbms_monitor_recovery_step(&m,&io,200)!=AMS_ADBMS_RESULT_OK);
            assert(f.write_count==writes);
        }
        assert(m.recovery.attempt_count==1);
    }
    puts("PASS Z019: config fingerprints, remote reset, stale epoch withdrawal, fresh recovery, SID mismatch, UNSNAP debt, exhaustive repair transfer faults, terminal no-retry");
}
