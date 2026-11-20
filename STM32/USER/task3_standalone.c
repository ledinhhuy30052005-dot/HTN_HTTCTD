/*
 * Task3 standalone harness ONLY.
 * This is not the implementation of teammate TaskN1 or TaskN2.
 * It publishes SAFE synthetic sensor data and advances the health counters for
 * the external tasks that are intentionally not part of this standalone project.
 * No manual UI commands are processed here; button events are drained so the
 * shared queue cannot fill in this isolated target.
 */
#include "task3_standalone.h"
#include "app_state.h"
#include "app_types.h"
#include "task.h"
#include "queue.h"
#include <string.h>

static void Task3StandaloneInputTask(void *argument)
{
    SensorData_t sample;
    ButtonEvent_t event;
    (void)argument;
    memset(&sample, 0, sizeof(sample));

    /* Safe sample: soil is not dry, the tank is OK, temperature is below
       fan/watering interlock thresholds, light is bright, and RTC scheduling
       is disabled. This avoids starting a pump merely by running the demo. */
    sample.soilRaw = 1400U;
    sample.soilPercent = 95U;
    sample.waterRaw = 2000U;
    sample.waterOK = 1U;
    sample.temperatureC = 10;
    sample.temperatureValid = 1U;
    sample.humidityPercent = 60U;
    sample.dhtValid = 1U;
    sample.lightLux = 2200U;
    sample.lightValid = 1U;
    sample.rtcValid = 0U;

    for (;;)
    {
        AppSensor_Update(&sample,
                         SENSOR_UPDATE_FAST | SENSOR_UPDATE_DHT11 |
                         SENSOR_UPDATE_LIGHT | SENSOR_UPDATE_RTC);

        /* Stand in for N1/N2 heartbeat sources so HealthTask can be exercised
           without adding those teammates' tasks to this standalone project. */
        AppHeartbeat_Increment(APP_HB_I2C_SENSOR);
        AppHeartbeat_Increment(APP_HB_SENSOR);
        AppHeartbeat_Increment(APP_HB_UI);

        /* TaskN1 normally consumes these events and applies menu semantics. */
        while ((g_buttonQueue != NULL) &&
               (xQueueReceive(g_buttonQueue, &event, 0U) == pdTRUE))
        {
            (void)event;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}

BaseType_t Task3Standalone_Create(void)
{
    return xTaskCreate(Task3StandaloneInputTask, "N3Stub", 192U,
                       NULL, 1U, NULL);
}
