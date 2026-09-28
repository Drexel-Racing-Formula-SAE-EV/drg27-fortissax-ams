#include "ams_z023_safety.h"
#include <ams_platform/supervision.h>
#include <ams_platform/fail_low.h>
void ams_z023_safety_cycle(void)
{
 ams_supervision_t decision;
 /* No output or watchdog authority is granted even when all records are good.
  * A rejected supervisor handoff also reinforces the physical inhibit. */
 if(!ams_z023_poll(&decision) || decision.inhibit)ams_bms_ok_force_low_direct();
}
