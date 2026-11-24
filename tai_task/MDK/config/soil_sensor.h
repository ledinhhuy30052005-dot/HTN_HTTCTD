#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H
#include <stdint.h>
void SoilSensor_Init(void);
uint8_t SoilSensor_Read(uint16_t *raw, uint8_t *percent, uint8_t *fault);
#endif
