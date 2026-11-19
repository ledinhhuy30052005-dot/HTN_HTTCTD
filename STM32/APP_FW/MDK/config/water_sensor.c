#include "water_sensor.h"
#include "app_config.h"
#include "analog_input.h"

static uint8_t s_waterOK = 0U;
void WaterSensor_Init(void) { s_waterOK = 0U; }

uint8_t WaterSensor_Read(uint16_t *raw, uint8_t *waterOK)
{
#if WATER_SENSOR_ENABLED
    uint16_t value;
    if ((raw == 0) || (waterOK == 0) || !AnalogInput_Read(1U, &value)) return 0U;
    *raw = value;
    if (s_waterOK)
    {
        if (value < (uint16_t)(WATER_RAW_MIN - WATER_RAW_HYST)) s_waterOK = 0U;
    }
    else if (value >= WATER_RAW_MIN) s_waterOK = 1U;
    *waterOK = s_waterOK;
    return 1U;
#else
    if (raw != 0) *raw = 0U;
    if (waterOK != 0) *waterOK = 1U;
    return (raw != 0 && waterOK != 0) ? 1U : 0U;
#endif
}
