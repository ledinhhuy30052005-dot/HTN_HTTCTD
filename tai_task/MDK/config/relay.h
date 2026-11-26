#ifndef RELAY_H
#define RELAY_H
#include <stdint.h>
void Relay_Init(void);
void Relay_SetFan(uint8_t on);
void Relay_SetLamp(uint8_t on);
void Relay_SetPump(uint8_t on);
void Relay_SetSpare(uint8_t on);
void Relay_AllOff(void);
#endif
