#ifndef APP_STATE_H
#define APP_STATE_H
#include "app_types.h"

typedef enum
{
    APP_HB_I2C_SENSOR = 0,
    APP_HB_SENSOR,
    APP_HB_CONTROL,
    APP_HB_BUTTON,
    APP_HB_UI,
    APP_HB_CONFIG,
    APP_HB_COUNT
} AppHeartbeatId_t;

uint8_t AppState_Init(void);
void AppSensor_Update(const SensorData_t *incoming, uint8_t updateMask);
void AppSensor_GetSnapshot(SensorData_t *out);
void AppHeartbeat_Increment(AppHeartbeatId_t id);
uint32_t AppHeartbeat_Get(AppHeartbeatId_t id);

#endif
