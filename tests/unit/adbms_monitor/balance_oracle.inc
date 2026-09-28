/* Exact v2.6.27 accumulator_plan_balance body with host-only topology shim.
 * Source: AMS/Core/Src/ext_drivers/accumulator.c. No hardware operations. */
#define NSMBS 1
#define NCELLS 15
#define BALANCE_START_MV 4100u
#define BALANCE_ON_DELTA_MV 20u
#define BALANCE_MAX_CELLS_PER_SEG 4u
typedef struct {
 bool smb_ready, voltage_full_usable;
 uint16_t min_voltage_mv, cell_voltage_mv[1][15];
} accumulator_t;
static bool accumulator_final_ring_topology_valid(const accumulator_t *d) { return d != NULL; }
static uint8_t accumulator_configured_smb_count(const accumulator_t *d) { (void)d; return 1; }
static bool accumulator_cell_voltage_usable(const accumulator_t *d, uint8_t ic, uint8_t c)
{ (void)ic; (void)c; return d->voltage_full_usable; }
bool accumulator_plan_balance(const accumulator_t *dev,
                              uint16_t planned_mask[NSMBS])
{
    if(planned_mask == NULL)
    {
        return false;
    }

    memset(planned_mask, 0, sizeof(uint16_t) * NSMBS);

    if((dev == NULL) || !dev->smb_ready ||
       !accumulator_final_ring_topology_valid(dev) ||
       !dev->voltage_full_usable || (dev->min_voltage_mv == 0u))
    {
        return false;
    }

    uint8_t ic_count = accumulator_configured_smb_count(dev);

    for(uint8_t ic = 0; ic < ic_count; ic++)
    {
        uint16_t cohort_min_mv = UINT16_MAX;
        uint8_t balance_count = 0u;

        for(uint8_t cell = 0; cell < NCELLS; cell++)
        {
            if(!accumulator_cell_voltage_usable(dev, ic, cell))
            {
                continue;
            }

            uint16_t cell_mv = dev->cell_voltage_mv[ic][cell];
            if((cell_mv >= BALANCE_START_MV) && (cell_mv < cohort_min_mv))
            {
                cohort_min_mv = cell_mv;
            }
        }

        if(cohort_min_mv == UINT16_MAX)
        {
            continue;
        }

        for(uint8_t cell = 0; cell < NCELLS; cell++)
        {
            if(!accumulator_cell_voltage_usable(dev, ic, cell))
            {
                continue;
            }

            uint16_t cell_mv = dev->cell_voltage_mv[ic][cell];
            if((cell_mv >= BALANCE_START_MV) &&
			   (cell_mv > cohort_min_mv) &&
			   ((uint16_t)(cell_mv - cohort_min_mv) > BALANCE_ON_DELTA_MV) &&
               (balance_count < BALANCE_MAX_CELLS_PER_SEG))
            {
                planned_mask[ic] |= (uint16_t)(1u << cell);
                balance_count++;
            }
        }
    }

    return true;
}
