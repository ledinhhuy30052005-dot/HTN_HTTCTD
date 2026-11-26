#include "taskN2.h"
#include "app_state.h"
#include "sensors.h"
#include "app_types.h"
#include "app_state.h"
#include "app_config.h"
#include "task.h"
#include <string.h>

static void TaskN2_SoilWaterDHTTask(void *argument)
{
    SensorData_t update;
    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastDht = 0U;
    (void)argument;
    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        AppSensor_GetSnapshot(&update);
        Sensors_ReadFast(&update);
        AppSensor_Update(&update, SENSOR_UPDATE_FAST);

        if ((now - lastDht) >= pdMS_TO_TICKS(DHT11_PERIOD_MS))
        {
            AppSensor_GetSnapshot(&update);
            Sensors_ReadDHT11(&update);
            AppSensor_Update(&update, SENSOR_UPDATE_DHT11);
            lastDht = now;
        }

        AppHeartbeat_Increment(APP_HB_SENSOR);
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

BaseType_t TaskN2_Create(void)
{
    return xTaskCreate(TaskN2_SoilWaterDHTTask, "SoilWaterDHT", 384U, NULL, 3U, NULL);
}
