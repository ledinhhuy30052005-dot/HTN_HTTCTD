#include "sensors.h"
#include "app_config.h"
#include "app_types.h"
#include "analog_input.h"
#include "soil_sensor.h"
#include "water_sensor.h"
#include "dht11.h"
#include "bh1750.h"
#include "ds3231.h"

static uint8_t s_lightReady = 0U;
static uint8_t s_lightFailures = 0U;
static uint8_t s_dhtFailures = 0U;
static uint8_t s_rtcFailures = 0U;

static void set_fault(SensorData_t *data, uint8_t mask, uint8_t fault)
{
    if (fault) data->faultFlags |= mask;
    else data->faultFlags &= (uint8_t)~mask;
}

void Sensors_Init(void)
{
    SoilSensor_Init();
    WaterSensor_Init();
    s_lightReady = 0U;
    s_lightFailures = 0U;
    s_dhtFailures = 0U;
    s_rtcFailures = 0U;
}

uint8_t Sensors_InitLight(void)
{
    s_lightReady = BH1750_Init();
    s_lightFailures = s_lightReady ? 0U : 3U;
    return s_lightReady;
}

void Sensors_ReadFast(SensorData_t *data)
{
    uint16_t soilRaw = 0U;
    uint16_t waterRaw = 0U;
    uint8_t soilPercent = 0U;
    uint8_t soilFault = 0U;
    uint8_t waterOK = 0U;
    uint8_t soilRead;
    uint8_t waterRead;
    if (data == 0) return;

    if (!AnalogInput_IsReady())
    {
        set_fault(data, SENSOR_FAULT_ADC, 1U);
        data->waterOK = 0U;
        return;
    }

    soilRead = SoilSensor_Read(&soilRaw, &soilPercent, &soilFault);
    waterRead = WaterSensor_Read(&waterRaw, &waterOK);
    if (soilRead)
    {
        data->soilRaw = soilRaw;
        data->soilPercent = soilPercent;
        set_fault(data, SENSOR_FAULT_SOIL, soilFault);
    }
    if (waterRead)
    {
        data->waterRaw = waterRaw;
        data->waterOK = waterOK;
    }
    else data->waterOK = 0U;

    set_fault(data, SENSOR_FAULT_ADC, (soilRead && waterRead) ? 0U : 1U);
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
