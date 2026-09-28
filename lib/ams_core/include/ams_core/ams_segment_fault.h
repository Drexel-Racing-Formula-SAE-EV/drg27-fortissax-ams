#ifndef AMS_SEGMENT_FAULT_H_
#define AMS_SEGMENT_FAULT_H_
#include <ams_core/ams_cell_image.h>
#include <ams_core/ams_temp_image.h>
/* Single populated segment. This never represents full-pack validity. */
typedef enum {
 AMS_SEG_NONE, AMS_SEG_NOT_READY, AMS_SEG_READ,
 AMS_SEG_OV_WARN, AMS_SEG_CHARGE_STOP, AMS_SEG_OV_HARD, AMS_SEG_OV_SEVERE,
 AMS_SEG_UV_WARN, AMS_SEG_UV_SOFT, AMS_SEG_UV_HARD, AMS_SEG_UV_SEVERE,
 AMS_SEG_HW_DISAGREE, AMS_SEG_HW_WARN, AMS_SEG_HOT_HARD, AMS_SEG_HOT_SEVERE,
 AMS_SEG_HOT_FAN, AMS_SEG_HOT_CHARGE, AMS_SEG_COLD_CHARGE
} ams_segment_fault_reason_t;
typedef struct {
 bool valid,read_fault,read_pending,warning,charge_stop,fan_max,pending,confirmed,latched;
 uint8_t read_streak;
 uint32_t pending_ms;
 ams_segment_fault_reason_t reason,pending_reason,latched_reason;
 uint16_t hw_disagreement,software_ov,software_uv;
} ams_segment_fault_t;
void ams_segment_voltage_fault(ams_segment_fault_t *s,const ams_cell_image_t *c,
 bool coherent,bool statd_valid,uint16_t ov,uint16_t uv,uint32_t now);
void ams_segment_temperature_fault(ams_segment_fault_t *s,const ams_temp_image_t *t,
 bool coherent,uint32_t now);
#endif
