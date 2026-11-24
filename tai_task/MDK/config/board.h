#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32f10x.h"

void Board_Init(void);
void Board_I2C1_Init(void);
void Board_I2C1_Recover(void);
void Board_TIM2_Init1MHz(void);
void Board_DelayUs(uint16_t us);
void Board_DelayMs(uint32_t ms);

#endif
