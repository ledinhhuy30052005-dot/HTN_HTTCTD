#ifndef WATER_SENSOR_H
#define WATER_SENSOR_H
#include <stdint.h>
void WaterSensor_Init(void);
uint8_t WaterSensor_Read(uint16_t *raw, uint8_t *waterOK);
#endif
