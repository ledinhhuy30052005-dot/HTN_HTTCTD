#include "sensors.h"
#include "app_config.h"
#include "app_types.h"
#include "board.h"
#include "dht11.h"
#include "bh1750.h"
#include "ds3231.h"

static uint8_t s_lightReady = 0U;
static uint8_t s_lightFailures = 0U;
static uint8_t s_dhtFailures = 0U;
static uint8_t s_rtcFailures = 0U;
static uint8_t s_waterOk = 0U;
static uint8_t s_soilRailCount = 0U;
static uint16_t s_soilSamples[3] = {0U, 0U, 0U};
static uint8_t s_soilSampleCount = 0U;
static uint8_t s_soilSampleIndex = 0U;

static void set_fault(SensorData_t *data, uint8_t mask, uint8_t fault)
{
    if (fault) data->faultFlags |= mask;
    else data->faultFlags &= (uint8_t)~mask;
}

static uint16_t average_soil_samples(void)
{
    uint32_t sum = 0UL;
    uint8_t i;
    if (s_soilSampleCount == 0U) return 0U;
    for (i = 0U; i < s_soilSampleCount; i++) sum += s_soilSamples[i];
    return (uint16_t)(sum / s_soilSampleCount);
}

static uint8_t raw_to_soil_percent(uint16_t raw)
{
    int32_t numerator;
    int32_t denominator;
    int32_t percent;
    denominator = (int32_t)SOIL_RAW_DRY - (int32_t)SOIL_RAW_WET;
    if (denominator == 0) return 0U;
    numerator = ((int32_t)SOIL_RAW_DRY - (int32_t)raw) * 100;
    percent = numerator / denominator;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (uint8_t)percent;
}

void Sensors_Init(void)
{
    s_lightReady = BH1750_Init();
    s_lightFailures = s_lightReady ? 0U : 3U;
    s_dhtFailures = 0U;
    s_rtcFailures = 0U;
    s_waterOk = 0U;
    s_soilRailCount = 0U;
    s_soilSampleCount = 0U;
    s_soilSampleIndex = 0U;
}

void Sensors_ReadFast(SensorData_t *data)
{
    uint16_t soilValue;
    uint16_t waterValue;
    if (data == 0) return;

    if (!Board_ADC_IsReady())
    {
        set_fault(data, SENSOR_FAULT_ADC, 1U);
        data->waterOK = 0U;
        return;
    }
    set_fault(data, SENSOR_FAULT_ADC, 0U);

    if (Board_ADC_Read(0U, &soilValue))
    {
        /* A few readings near an ADC rail usually indicate a missing/shorted soil signal.
         * The analog input has no digital ACK, so this is only a plausibility check. */
        if ((soilValue <= 5U) || (soilValue >= 4090U))
        {
            if (s_soilRailCount < 5U) s_soilRailCount++;
        }
        else s_soilRailCount = 0U;
        set_fault(data, SENSOR_FAULT_SOIL, (s_soilRailCount >= 5U) ? 1U : 0U);

        s_soilSamples[s_soilSampleIndex] = soilValue;
        s_soilSampleIndex = (uint8_t)((s_soilSampleIndex + 1U) % 3U);
        if (s_soilSampleCount < 3U) s_soilSampleCount++;
        data->soilRaw = average_soil_samples();
        data->soilPercent = raw_to_soil_percent(data->soilRaw);
    }
    else
    {
        set_fault(data, SENSOR_FAULT_ADC, 1U);
    }

    /* Water probe is sampled at the fast 100 ms task rate. Use a small hysteresis
     * so a falling water signal stops the pump quickly without relay chatter. */
#if WATER_SENSOR_ENABLED
    if (Board_ADC_Read(1U, &waterValue))
    {
        data->waterRaw = waterValue;
        if (s_waterOk)
        {
            if (waterValue < (uint16_t)(WATER_RAW_MIN - WATER_RAW_HYST)) s_waterOk = 0U;
        }
        else if (waterValue >= WATER_RAW_MIN)
        {
            s_waterOk = 1U;
        }
        data->waterOK = s_waterOk;
    }
    else
    {
        data->waterOK = 0U;
        set_fault(data, SENSOR_FAULT_ADC, 1U);
    }
#else
    data->waterOK = 1U;
    data->waterRaw = 0U;
#endif
}

void Sensors_ReadDHT11(SensorData_t *data)
{
    int16_t temperature;
    uint8_t humidity;
    if (data == 0) return;
    if (DHT11_Read(&temperature, &humidity))
    {
        data->temperatureC = temperature;
        data->humidityPercent = humidity;
        data->temperatureValid = 1U;
        data->dhtValid = 1U;
        s_dhtFailures = 0U;
        set_fault(data, SENSOR_FAULT_DHT11, 0U);
    }
    else
    {
        data->dhtValid = 0U;
        if (s_dhtFailures < 3U) s_dhtFailures++;
        if (s_dhtFailures >= 3U)
        {
            data->temperatureValid = 0U;
            set_fault(data, SENSOR_FAULT_DHT11, 1U);
        }
    }
}

void Sensors_ReadLight(SensorData_t *data)
{
    uint16_t lux;
    if (data == 0) return;
    if (!s_lightReady) s_lightReady = BH1750_Init();
    if (s_lightReady && BH1750_ReadLux(&lux))
    {
        data->lightLux = lux;
        data->lightValid = 1U;
        s_lightFailures = 0U;
        set_fault(data, SENSOR_FAULT_BH1750, 0U);
    }
    else
    {
        data->lightValid = 0U;
        s_lightReady = 0U;
        if (s_lightFailures < 3U) s_lightFailures++;
        if (s_lightFailures >= 3U) set_fault(data, SENSOR_FAULT_BH1750, 1U);
    }
}

void Sensors_ReadRTC(SensorData_t *data)
{
    DS3231_Time_t t;
    if (data == 0) return;
    if (DS3231_ReadTime(&t))
    {
        data->second = t.second;
        data->minute = t.minute;
        data->hour = t.hour;
        data->day = t.day;
        data->month = t.month;
        data->year = t.year;
        data->dayKey = ((uint32_t)t.year * 10000UL) + ((uint32_t)t.month * 100UL) + t.day;
        data->rtcValid = 1U;
        s_rtcFailures = 0U;
        set_fault(data, SENSOR_FAULT_RTC, 0U);
    }
    else
    {
        data->rtcValid = 0U;
        if (s_rtcFailures < 3U) s_rtcFailures++;
        if (s_rtcFailures >= 3U) set_fault(data, SENSOR_FAULT_RTC, 1U);
    }
}
