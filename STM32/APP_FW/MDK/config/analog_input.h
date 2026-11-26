#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H
#include <stdint.h>
uint8_t AnalogInput_Init(void);
uint8_t AnalogInput_IsReady(void);
/* index 0 = PA0 soil, index 1 = PA2 water-level sensor */
uint8_t AnalogInput_Read(uint8_t index, uint16_t *value);
#endif
