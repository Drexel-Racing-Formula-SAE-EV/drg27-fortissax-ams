#include <ams_core/ams_adbms_link.h>
#include <string.h>
uint16_t ams_link_pec15(const uint8_t *data, size_t length)
{
 uint16_t r = 16U;
 for (size_t i=0; i<length; ++i) {
  r ^= (uint16_t)((uint16_t)data[i] << 7);
  for (unsigned b=0; b<8; ++b)
   r = (uint16_t)((r << 1) ^ ((r & 0x4000U) ? 0x4599U : 0U));
 }
 return (uint16_t)(r << 1);
}
uint16_t ams_link_pec10(const uint8_t data[6], uint8_t counter)
{
 uint16_t r=16U;
 for (unsigned i=0; i<6; ++i) {
  r ^= (uint16_t)((uint16_t)data[i] << 2);
  for (unsigned b=0; b<8; ++b)
   r=(uint16_t)((r<<1)^((r & 0x200U)?0x8fU:0U));
 }
 r ^= (uint16_t)((counter & 63U) << 4);
 for (unsigned b=0; b<6; ++b)
  r=(uint16_t)((r<<1)^((r & 0x200U)?0x8fU:0U));
 return r & 0x3ffU;
}
void ams_link_invalidate(ams_link_t *link)
{
 if (link != NULL) memset(link,0,sizeof(*link));
}
static bool io_valid(const ams_link_io_t *io)
{
 return io != NULL && io->now_us != NULL && io->wake != NULL && io->read != NULL;
}
ams_link_result_t ams_link_wake(ams_link_t *l,const ams_link_io_t *io,bool cold)
{
 if (l == NULL || !io_valid(io)) return AMS_LINK_INVALID;
 /* A wake can traverse sleep/reset. Never carry counter trust across it. */
 ams_link_invalidate(l);
 if (!io->wake(io->context,cold)) return AMS_LINK_IO;
 if (!io->now_us(io->context,&l->last_us)) return AMS_LINK_CLOCK;
 /* Cold settle exceeds isoSPI's minimum idle timeout. The following
  * standalone read must send a fresh normal wake, as in the oracle. */
 l->session_valid=!cold;
 return AMS_LINK_OK;
}
ams_link_result_t ams_link_read(ams_link_t *l,const ams_link_io_t *io,
                              ams_link_read_t kind,bool guarded,ams_link_sample_t *out)
{
 uint64_t now;
 uint8_t cmd[4]={0},rx[8]={0};
 uint16_t pec;
 ams_link_result_t result;
 if (out == NULL) return AMS_LINK_INVALID;
 memset(out,0,sizeof(*out));
 if (l == NULL || !io_valid(io) || (kind != AMS_LINK_SID && kind != AMS_LINK_CFGA))
  return AMS_LINK_INVALID;
 if (!io->now_us(io->context,&now)) { ams_link_invalidate(l); return AMS_LINK_CLOCK; }
 if (!l->session_valid || now < l->last_us || now-l->last_us >= AMS_ADBMS_LINK_GUARD_US) {
  ams_link_invalidate(l);
  if (guarded) return AMS_LINK_SESSION_EXPIRED;
  result=ams_link_wake(l,io,false);
  if (result != AMS_LINK_OK) return result;
 }
 cmd[1]=(kind==AMS_LINK_SID)?0x2cU:0x02U;
 pec=ams_link_pec15(cmd,2);cmd[2]=(uint8_t)(pec>>8);cmd[3]=(uint8_t)pec;
 if (!io->read(io->context,cmd,rx)) { ams_link_invalidate(l); return AMS_LINK_IO; }
 if (!io->now_us(io->context,&now) || now < l->last_us) {
  ams_link_invalidate(l);return AMS_LINK_CLOCK;
 }
 l->last_us=now;
 uint8_t counter=rx[6]>>2;
 pec=(uint16_t)(((rx[6]&3U)<<8)|rx[7]);
 if (pec!=ams_link_pec10(rx,counter)) {
  ams_link_invalidate(l);return AMS_LINK_PEC;
 }
 /* These two read-only commands do not advance the hardware counter.
  * Adopt a PEC-valid mismatch for the next read, reject the present read. */
 bool mismatch=l->counter_valid && l->counter!=counter;
 l->counter=counter;l->counter_valid=true;
 if (mismatch) return AMS_LINK_COUNTER;
 if (kind==AMS_LINK_SID && ((rx[1]>>1)&63U)!=3U) return AMS_LINK_IDENTITY;
 memcpy(out->data,rx,6);out->counter=counter;out->valid=true;
 return AMS_LINK_OK;
}
