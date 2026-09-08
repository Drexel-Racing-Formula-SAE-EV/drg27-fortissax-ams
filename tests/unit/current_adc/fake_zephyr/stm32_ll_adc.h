#ifndef STM32_LL_ADC_H_
#define STM32_LL_ADC_H_
#include <stdint.h>
#include "fake_zephyr_adc.h"

#define LL_ADC_FLAG_EOCS (1U << 1)
#define LL_ADC_FLAG_STRT (1U << 4)
#define LL_ADC_FLAG_OVR  (1U << 5)
/* ADC_SR is write-zero-to-clear for these status flags. */
#define WRITE_REG(reg, value) ((reg) &= (uint32_t)(value))

#define LL_ADC_CHANNEL_3 3U
#define LL_ADC_CHANNEL_10 10U
#define LL_ADC_CLOCK_SYNC_PCLK_DIV6 6U
#define LL_ADC_MULTI_INDEPENDENT 0U
#define LL_ADC_MULTI_REG_DMA_EACH_ADC 0U
#define LL_ADC_RESOLUTION_12B 12U
#define LL_ADC_DATA_ALIGN_RIGHT 0U
#define LL_ADC_SEQ_SCAN_DISABLE 0U
#define LL_ADC_REG_TRIG_SOFTWARE 0U
#define LL_ADC_REG_SEQ_SCAN_DISABLE 0U
#define LL_ADC_REG_SEQ_DISCONT_DISABLE 0U
#define LL_ADC_REG_CONV_SINGLE 0U
#define LL_ADC_REG_DMA_TRANSFER_NONE 0U
#define LL_ADC_REG_FLAG_EOC_UNITARY_CONV 1U
#define LL_ADC_REG_RANK_1 1U
#define LL_ADC_SAMPLINGTIME_480CYCLES 480U
#define __LL_ADC_CHANNEL_TO_DECIMAL_NB(ch) (ch)

static inline void LL_ADC_Disable(ADC_TypeDef *a) { a->enabled=0U; }
static inline void LL_ADC_Enable(ADC_TypeDef *a) {
    if ((a == &fake_adc1_regs && fake_adc.high_enable_fail) ||
        (a == &fake_adc2_regs && fake_adc.low_enable_fail)) return;
    a->enabled=1U;
}
static inline uint32_t LL_ADC_IsEnabled(ADC_TypeDef *a) { return a->enabled; }

#define ADC_SETGET(prefix, field) \
static inline void LL_ADC_Set##prefix(ADC_TypeDef*a,uint32_t v){a->field=v;} \
static inline uint32_t LL_ADC_Get##prefix(ADC_TypeDef*a){return a->field;}
ADC_SETGET(Resolution,resolution)
ADC_SETGET(DataAlignment,alignment)
ADC_SETGET(SequencersScanMode,scan_mode)
#undef ADC_SETGET

static inline void LL_ADC_REG_SetTriggerSource(ADC_TypeDef*a,uint32_t v){a->trigger=v;}
static inline uint32_t LL_ADC_REG_GetTriggerSource(ADC_TypeDef*a){return a->trigger;}
static inline void LL_ADC_REG_SetSequencerLength(ADC_TypeDef*a,uint32_t v){a->seq_len=v;}
static inline uint32_t LL_ADC_REG_GetSequencerLength(ADC_TypeDef*a){return a->seq_len;}
static inline void LL_ADC_REG_SetSequencerDiscont(ADC_TypeDef*a,uint32_t v){a->discont=v;}
static inline uint32_t LL_ADC_REG_GetSequencerDiscont(ADC_TypeDef*a){return a->discont;}
static inline void LL_ADC_REG_SetContinuousMode(ADC_TypeDef*a,uint32_t v){a->continuous=v;}
static inline uint32_t LL_ADC_REG_GetContinuousMode(ADC_TypeDef*a){return a->continuous;}
static inline void LL_ADC_REG_SetDMATransfer(ADC_TypeDef*a,uint32_t v){a->dma=v;}
static inline uint32_t LL_ADC_REG_GetDMATransfer(ADC_TypeDef*a){return a->dma;}
static inline void LL_ADC_REG_SetFlagEndOfConversion(ADC_TypeDef*a,uint32_t v){a->eoc_mode=v;}
static inline uint32_t LL_ADC_REG_GetFlagEndOfConversion(ADC_TypeDef*a){return a->eoc_mode;}
static inline void LL_ADC_REG_SetSequencerRanks(ADC_TypeDef*a,uint32_t rank,uint32_t ch){(void)rank;a->rank1=ch;}
static inline uint32_t LL_ADC_REG_GetSequencerRanks(ADC_TypeDef*a,uint32_t rank){(void)rank;return a->rank1;}
static inline void LL_ADC_SetChannelSamplingTime(ADC_TypeDef*a,uint32_t ch,uint32_t v){ if(ch==3U)a->sample_time_ch3=v; else if(ch==10U)a->sample_time_ch10=v; }
static inline uint32_t LL_ADC_GetChannelSamplingTime(ADC_TypeDef*a,uint32_t ch){ return ch==3U?a->sample_time_ch3:a->sample_time_ch10; }

static inline void LL_ADC_DisableIT_EOCS(ADC_TypeDef*a){a->it_eocs=0U;}
static inline void LL_ADC_DisableIT_OVR(ADC_TypeDef*a){a->it_ovr=0U;}
static inline void LL_ADC_DisableIT_JEOS(ADC_TypeDef*a){a->it_jeos=0U;}
static inline void LL_ADC_DisableIT_AWD1(ADC_TypeDef*a){a->it_awd1=0U;}
static inline uint32_t LL_ADC_IsEnabledIT_EOCS(ADC_TypeDef*a){return a->it_eocs;}
static inline uint32_t LL_ADC_IsEnabledIT_OVR(ADC_TypeDef*a){return a->it_ovr;}
static inline uint32_t LL_ADC_IsEnabledIT_JEOS(ADC_TypeDef*a){return a->it_jeos;}
static inline uint32_t LL_ADC_IsEnabledIT_AWD1(ADC_TypeDef*a){return a->it_awd1;}

static inline void LL_ADC_ClearFlag_EOCS(ADC_TypeDef*a){a->flag_eoc=0U;a->SR&=~LL_ADC_FLAG_EOCS;}
static inline void LL_ADC_ClearFlag_OVR(ADC_TypeDef*a){a->flag_ovr=0U;a->SR&=~LL_ADC_FLAG_OVR;}

static inline void LL_ADC_REG_StartConversionSWStart(ADC_TypeDef*a){
    a->SR |= LL_ADC_FLAG_STRT;
    if(a==&fake_adc1_regs){fake_adc.high_start_ms=fake_adc.now_ms; fake_adc.start_high_count++; fake_adc_trace(FAKE_ADC_EVT_START_HIGH);} 
    else {fake_adc.low_start_ms=fake_adc.now_ms; fake_adc.start_low_count++; fake_adc_trace(FAKE_ADC_EVT_START_LOW);} 
}
static inline uint32_t LL_ADC_IsActiveFlag_EOCS(ADC_TypeDef*a){
    uint32_t result;
    if (fake_adc.reentry_hook && !fake_adc.reentry_hook_fired) { fake_adc.reentry_hook_fired=true; fake_adc.reentry_hook(); }
    if(a==&fake_adc1_regs){
        if(!fake_adc.high_stuck &&
           (uint32_t)(fake_adc.now_ms-fake_adc.high_start_ms)>=fake_adc.high_complete_after_ms){a->flag_eoc=1U;a->SR|=LL_ADC_FLAG_EOCS;a->data=fake_adc.high_count;}
    } else {
        if(!fake_adc.low_stuck &&
           (uint32_t)(fake_adc.now_ms-fake_adc.low_start_ms)>=fake_adc.low_complete_after_ms){a->flag_eoc=1U;a->SR|=LL_ADC_FLAG_EOCS;a->data=fake_adc.low_count;}
    }
    result = a->flag_eoc;
    /* Simulate hardware time advancing independently while the CPU polls.
     * k_uptime_get_32() itself is observational and must not create/erase
     * EOC/OVR races merely because production added another timestamp read. */
    fake_adc.now_ms++;
    return result;
}
static inline uint32_t LL_ADC_IsActiveFlag_OVR(ADC_TypeDef*a){
    if(a==&fake_adc1_regs && fake_adc.high_ovr){a->flag_ovr=1U;a->SR|=LL_ADC_FLAG_OVR;}
    if(a==&fake_adc2_regs && fake_adc.low_ovr){a->flag_ovr=1U;a->SR|=LL_ADC_FLAG_OVR;}
    return a->flag_ovr;
}
static inline uint16_t LL_ADC_REG_ReadConversionData12(ADC_TypeDef*a){return (uint16_t)(a->data & 0x0FFFU);}

static inline void LL_ADC_SetCommonClock(ADC_Common_TypeDef*c,uint32_t v){c->clock=v;}
static inline uint32_t LL_ADC_GetCommonClock(ADC_Common_TypeDef*c){return (fake_adc.corrupt_after_reset && fake_adc.reset_count > 1U) ? 0xDEADU : c->clock;}
static inline void LL_ADC_SetMultimode(ADC_Common_TypeDef*c,uint32_t v){c->multimode=v;}
static inline uint32_t LL_ADC_GetMultimode(ADC_Common_TypeDef*c){return c->multimode;}
static inline void LL_ADC_SetMultiDMATransfer(ADC_Common_TypeDef*c,uint32_t v){c->multi_dma=v;}
static inline uint32_t LL_ADC_GetMultiDMATransfer(ADC_Common_TypeDef*c){return c->multi_dma;}
#endif
