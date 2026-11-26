#include "app_types.h"
#include "app_state.h"
#include "app_config.h"
#include "task.h"
#include "queue.h"
#include "i2c_bus.h"
#include "config_store.h"
#include "sensors.h"
#include <string.h>

QueueHandle_t g_buttonQueue = NULL;
QueueHandle_t g_sensorQueue = NULL;
QueueHandle_t g_configSaveQueue = NULL;
SemaphoreHandle_t g_configMutex = NULL;
SemaphoreHandle_t g_i2cMutex = NULL;
SemaphoreHandle_t g_sensorDataMutex = NULL;
static volatile uint32_t s_heartbeat[APP_HB_COUNT] = {0U};
volatile uint8_t g_configSaveState = 0U;
volatile uint8_t g_pumpOn = 0U;
volatile uint8_t g_fanOn = 0U;
volatile uint8_t g_lampOn = 0U;
volatile uint8_t g_tankLowAlarm = 0U;
volatile uint8_t g_pumpStatus = 0U;
volatile uint8_t g_manualMode = 0U; /* 0=AUTO, 1=MANUAL; boots safe in AUTO */
volatile uint8_t g_manualPumpRequest = 0U;
volatile uint8_t g_manualFanRequest = 0U;
volatile uint8_t g_manualLampRequest = 0U;

static AppConfig_t s_activeConfig;

void AppConfig_Default(AppConfig_t *c)
{
    if (c == 0) return;
    memset(c, 0, sizeof(*c));
    c->magic = APP_CONFIG_MAGIC;
    c->version = APP_CONFIG_VERSION;
    c->soilOnPercent = DEFAULT_SOIL_ON_PERCENT;
    c->soilOffPercent = DEFAULT_SOIL_OFF_PERCENT;
    c->fanOnTempC = DEFAULT_FAN_TEMP_C;
    c->waterBlockTempC = DEFAULT_WATER_BLOCK_TEMP_C;
    c->lightOnLux = DEFAULT_LIGHT_ON_LUX;
    c->schedule1Minute = DEFAULT_SCHEDULE_1_MIN;
    c->schedule2Minute = DEFAULT_SCHEDULE_2_MIN;
}

uint8_t AppConfig_Validate(const AppConfig_t *c)
{
    if (c == 0) return 0U;
    if (c->magic != APP_CONFIG_MAGIC || c->version != APP_CONFIG_VERSION) return 0U;
    if (c->soilOnPercent < 5U || c->soilOnPercent > 70U) return 0U;
    if (c->soilOffPercent <= c->soilOnPercent || c->soilOffPercent > 95U) return 0U;
    if (c->fanOnTempC < 15U || c->fanOnTempC > 50U) return 0U;
    if (c->waterBlockTempC < 20U || c->waterBlockTempC > 60U) return 0U;
    if (c->lightOnLux > 2000U) return 0U;
    if (c->schedule1Minute >= 1440U || c->schedule2Minute >= 1440U) return 0U;
    return 1U;
}

void AppConfig_GetActive(AppConfig_t *c)
{
    if (c == 0) return;
    if ((g_configMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
        (void)xSemaphoreTake(g_configMutex, portMAX_DELAY);
    *c = s_activeConfig;
    if ((g_configMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
        (void)xSemaphoreGive(g_configMutex);
}

void AppConfig_SetActive(const AppConfig_t *c)
{
    if (c == 0) return;
    if ((g_configMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
        (void)xSemaphoreTake(g_configMutex, portMAX_DELAY);
    s_activeConfig = *c;
    if ((g_configMutex != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
        (void)xSemaphoreGive(g_configMutex);
}


uint8_t AppState_Init(void)
{
    AppConfig_t initialConfig;

    g_buttonQueue = xQueueCreate(8U, sizeof(ButtonEvent_t));
    g_sensorQueue = xQueueCreate(1U, sizeof(SensorData_t));
    g_configSaveQueue = xQueueCreate(1U, sizeof(AppConfig_t));
    g_configMutex = xSemaphoreCreateMutex();
    g_i2cMutex = xSemaphoreCreateMutex();
    g_sensorDataMutex = xSemaphoreCreateMutex();
    if ((g_buttonQueue == NULL) || (g_sensorQueue == NULL) ||
        (g_configSaveQueue == NULL) || (g_configMutex == NULL) ||
        (g_i2cMutex == NULL) || (g_sensorDataMutex == NULL))
        return 0U;

    I2C1_SetMutex(g_i2cMutex);
    Sensors_Init();
    ConfigStore_Load(&initialConfig);
    AppConfig_SetActive(&initialConfig);
    return 1U;
}

void AppSensor_GetSnapshot(SensorData_t *out)
{
    if (out == NULL) return;
    if ((g_sensorQueue == NULL) || (xQueuePeek(g_sensorQueue, out, 0U) != pdTRUE))
        memset(out, 0, sizeof(*out));
}

static void merge_sensor_data(SensorData_t *dst, const SensorData_t *src, uint8_t mask)
{
    uint8_t faultMask = 0U;
    if (mask & SENSOR_UPDATE_FAST)
    {
        dst->soilRaw = src->soilRaw;
        dst->soilPercent = src->soilPercent;
        dst->waterRaw = src->waterRaw;
        dst->waterOK = src->waterOK;
        faultMask |= (SENSOR_FAULT_ADC | SENSOR_FAULT_SOIL);
    }
    if (mask & SENSOR_UPDATE_DHT11)
    {
        dst->temperatureC = src->temperatureC;
        dst->temperatureValid = src->temperatureValid;
        dst->humidityPercent = src->humidityPercent;
        dst->dhtValid = src->dhtValid;
        faultMask |= SENSOR_FAULT_DHT11;
    }
    if (mask & SENSOR_UPDATE_LIGHT)
    {
        dst->lightLux = src->lightLux;
        dst->lightValid = src->lightValid;
        faultMask |= SENSOR_FAULT_BH1750;
    }
    if (mask & SENSOR_UPDATE_RTC)
    {
        dst->rtcValid = src->rtcValid;
        dst->hour = src->hour;
        dst->minute = src->minute;
        dst->second = src->second;
        dst->day = src->day;
        dst->month = src->month;
        dst->year = src->year;
        dst->dayKey = src->dayKey;
        faultMask |= SENSOR_FAULT_RTC;
    }
    dst->faultFlags = (uint8_t)((dst->faultFlags & (uint8_t)~faultMask) |
                                (src->faultFlags & faultMask));
}

void AppSensor_Update(const SensorData_t *incoming, uint8_t updateMask)
{
    SensorData_t current;
    if ((incoming == NULL) || (g_sensorQueue == NULL)) return;
    if (g_sensorDataMutex != NULL) (void)xSemaphoreTake(g_sensorDataMutex, portMAX_DELAY);
    if (xQueuePeek(g_sensorQueue, &current, 0U) != pdTRUE)
        memset(&current, 0, sizeof(current));
    merge_sensor_data(&current, incoming, updateMask);
    (void)xQueueOverwrite(g_sensorQueue, &current);
    if (g_sensorDataMutex != NULL) (void)xSemaphoreGive(g_sensorDataMutex);
}

void AppHeartbeat_Increment(AppHeartbeatId_t id)
{
    if ((uint32_t)id < (uint32_t)APP_HB_COUNT) s_heartbeat[id]++;
}

uint32_t AppHeartbeat_Get(AppHeartbeatId_t id)
{
    if ((uint32_t)id < (uint32_t)APP_HB_COUNT) return s_heartbeat[id];
    return 0U;
}
