#ifndef AMS_ADBMS_LINK_PROBE_H
#define AMS_ADBMS_LINK_PROBE_H
/* Owner thread only. Finite read-only probe; no request pointers or raw API. */
void ams_adbms_link_probe_step(void);
#endif
