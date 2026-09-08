#ifndef STM32_LL_SPI_H_
#define STM32_LL_SPI_H_
#include <stdint.h>
#include "fake_zephyr_spi.h"
#define LL_SPI_MODE_MASTER 1U
#define LL_SPI_FULL_DUPLEX 2U
#define LL_SPI_DATAWIDTH_8BIT 8U
#define LL_SPI_POLARITY_HIGH 1U
#define LL_SPI_PHASE_2EDGE 2U
#define LL_SPI_NSS_SOFT 1U
#define LL_SPI_BAUDRATEPRESCALER_DIV256 256U
#define LL_SPI_MSB_FIRST 0U
#define LL_SPI_PROTOCOL_MOTOROLA 0U
#define LL_SPI_RX_FIFO_TH_QUARTER 1U
static inline void LL_SPI_Disable(SPI_TypeDef *s){s->enabled=0;}
static inline void LL_SPI_Enable(SPI_TypeDef *s){s->enabled=1;}
static inline uint32_t LL_SPI_IsEnabled(SPI_TypeDef *s){return s->enabled;}
static inline void LL_SPI_DisableIT_TXE(SPI_TypeDef *s){s->it_txe=0;}
static inline void LL_SPI_DisableIT_RXNE(SPI_TypeDef *s){s->it_rxne=0;}
static inline void LL_SPI_DisableIT_ERR(SPI_TypeDef *s){s->it_err=0;}
static inline uint32_t LL_SPI_IsEnabledIT_TXE(SPI_TypeDef *s){return s->it_txe;}
static inline uint32_t LL_SPI_IsEnabledIT_RXNE(SPI_TypeDef *s){return s->it_rxne;}
static inline uint32_t LL_SPI_IsEnabledIT_ERR(SPI_TypeDef *s){return s->it_err;}
#define SETGET(name,field) static inline void LL_SPI_Set##name(SPI_TypeDef*s,uint32_t v){s->field=v;} static inline uint32_t LL_SPI_Get##name(SPI_TypeDef*s){return s->field;}
SETGET(Mode,mode)
SETGET(TransferDirection,direction)
SETGET(DataWidth,data_width)
SETGET(ClockPolarity,polarity)
SETGET(ClockPhase,phase)
SETGET(NSSMode,nss_mode)
SETGET(BaudRatePrescaler,prescaler)
SETGET(TransferBitOrder,bit_order)
SETGET(Standard,standard)
SETGET(RxFIFOThreshold,rx_threshold)
#undef SETGET
static inline void LL_SPI_DisableCRC(SPI_TypeDef*s){s->crc_enabled=0;}
static inline uint32_t LL_SPI_IsEnabledCRC(SPI_TypeDef*s){return s->crc_enabled;}
static inline void LL_SPI_DisableNSSPulseMgt(SPI_TypeDef*s){s->nss_pulse=0;}
static inline uint32_t LL_SPI_IsEnabledNSSPulse(SPI_TypeDef*s){return s->nss_pulse;}
static inline uint32_t LL_SPI_IsActiveFlag_TXE(SPI_TypeDef*s){(void)s; if(fake_spi.txe_hook && !fake_spi.txe_hook_fired){fake_spi.txe_hook_fired=true; fake_spi.txe_hook();} return fake_spi.txe_stuck?0U:1U;}
static inline uint32_t LL_SPI_IsActiveFlag_RXNE(SPI_TypeDef*s){(void)s; return fake_spi.rxne_stuck?0U:(fake_spi.fifo_pending != 0U);}
static inline uint32_t LL_SPI_IsActiveFlag_BSY(SPI_TypeDef*s){(void)s; return fake_spi.bsy_stuck?1U:0U;}
static inline uint32_t LL_SPI_IsActiveFlag_OVR(SPI_TypeDef*s){(void)s; return fake_spi.fault_ovr?1U:0U;}
static inline uint32_t LL_SPI_IsActiveFlag_MODF(SPI_TypeDef*s){(void)s; return 0U;}
static inline uint32_t LL_SPI_IsActiveFlag_CRCERR(SPI_TypeDef*s){(void)s; return 0U;}
static inline uint32_t LL_SPI_IsActiveFlag_FRE(SPI_TypeDef*s){(void)s; return 0U;}
static inline void LL_SPI_TransmitData8(SPI_TypeDef*s,uint8_t v){s->data=v; if(fake_spi.tx_count<sizeof(fake_spi.tx_log)) fake_spi.tx_log[fake_spi.tx_count]=v; fake_spi.tx_count++; fake_spi.fifo_pending++;}
static inline uint8_t LL_SPI_ReceiveData8(SPI_TypeDef*s){fake_spi.rx_count++; if(fake_spi.fifo_pending) fake_spi.fifo_pending--; return (uint8_t)(s->data ^ 0x5AU);}
#endif
