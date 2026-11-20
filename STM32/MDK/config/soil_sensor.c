#include "soil_sensor.h"
#include "app_config.h"
#include "analog_input.h"

static uint8_t s_railCount = 0U;
static uint16_t s_samples[3] = {0U, 0U, 0U};
static uint8_t s_sampleCount = 0U;
static uint8_t s_sampleIndex = 0U;

void SoilSensor_Init(void)
{
    s_railCount = 0U;
    s_sampleCount = 0U;
    s_sampleIndex = 0U;
    s_samples[0] = s_samples[1] = s_samples[2] = 0U;
}

static uint16_t average_samples(void)
{
    uint32_t sum = 0UL;
    uint8_t i;
    if (s_sampleCount == 0U) return 0U;
    for (i = 0U; i < s_sampleCount; i++) sum += s_samples[i];
    return (uint16_t)(sum / s_sampleCount);
}

static uint8_t raw_to_percent(uint16_t raw)
{
    int32_t denominator = (int32_t)SOIL_RAW_DRY - (int32_t)SOIL_RAW_WET;
    int32_t percent;
    if (denominator == 0) return 0U;
    percent = (((int32_t)SOIL_RAW_DRY - (int32_t)raw) * 100) / denominator;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (uint8_t)percent;
}

uint8_t SoilSensor_Read(uint16_t *raw, uint8_t *percent, uint8_t *fault)
{
    uint16_t sample;
    if ((raw == 0) || (percent == 0) || (fault == 0)) return 0U;
    if (!AnalogInput_Read(0U, &sample)) return 0U;
    if ((sample <= 5U) || (sample >= 4090U))
    {
        if (s_railCount < 5U) s_railCount++;
    }
    else s_railCount = 0U;
    *fault = (s_railCount >= 5U) ? 1U : 0U;
    s_samples[s_sampleIndex] = sample;
    s_sampleIndex = (uint8_t)((s_sampleIndex + 1U) % 3U);
    if (s_sampleCount < 3U) s_sampleCount++;
    *raw = average_samples();
    *percent = raw_to_percent(*raw);
    return 1U;
}
