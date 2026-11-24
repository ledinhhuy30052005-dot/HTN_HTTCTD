#ifndef BUTTON_H
#define BUTTON_H
#include <stdint.h>
void Button_Init(void);
/* index 0=OK, 1=UP, 2=DOWN, 3=BACK; returns 1 when pressed */
uint8_t Button_IsPressed(uint8_t index);
#endif
