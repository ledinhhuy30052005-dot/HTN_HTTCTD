#ifndef DHT11_H
#define DHT11_H
#include <stdint.h>

/* Read DHT11. Returns 1 on valid checksum; temperature is Celsius and
 * humidity is relative humidity in whole percent. Sensor data pin: PA3.
 */
uint8_t DHT11_Read(int16_t *temperatureC, uint8_t *humidityPercent);

#endif
