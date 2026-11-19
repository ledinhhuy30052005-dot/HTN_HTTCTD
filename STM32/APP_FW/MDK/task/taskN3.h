#ifndef TASKN3_H
#define TASKN3_H
#include <stdint.h>
#include "FreeRTOS.h"
uint8_t TaskN3_InitShared(void);
BaseType_t TaskN3_Create(void);
#endif
