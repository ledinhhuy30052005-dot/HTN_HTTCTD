#ifndef DHT11_H
#define DHT11_H
#include <stdint.h>
uint8_t DHT11_Read(int16_t *temperatureC, uint8_t *humidityPercent);
#endif
