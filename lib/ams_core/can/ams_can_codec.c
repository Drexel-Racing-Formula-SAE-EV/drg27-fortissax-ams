#include <ams_core/ams_can_codec.h>
#include <string.h>
bool ams_can_encode_current_diagnostic(const ams_can_current_provenance_t *p,
 uint32_t now,ams_can_tx_frame_t *f)
{
 if(!p || !f) return false;
 memset(f,0,sizeof(*f)); f->id=0x68b; f->dlc=8;
 f->tx_class=AMS_CAN_TX_CLASS_PROTECTED_ADVISORY; f->data[3]=1;
 uint32_t age=now-p->latest_sample_ms;
 if(!p->valid || age>100) return true;
 bool calibrated=p->calibration_confident && p->calibration_id &&
                 p->uncertainty_ma && p->uncertainty_ma!=UINT16_MAX;
 f->data[0]=1; f->data[1]=calibrated?2:1; f->data[2]=1;
 f->data[4]=(uint8_t)(p->sequence>>8); f->data[5]=(uint8_t)p->sequence;
 f->data[6]=(uint8_t)(age>>8); f->data[7]=(uint8_t)age;
 return true;
}
