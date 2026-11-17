#ifndef OLED_H
#define OLED_H
#include <stdint.h>

#define OLED_COLUMNS 21U

uint8_t OLED_Init(void);
uint8_t OLED_IsReady(void);
void OLED_Clear(void);
void OLED_WriteLine(uint8_t page, const char *text);                 /* full width: 21 chars */
void OLED_WriteCenteredLine(uint8_t page, const char *text);          /* centered full-width title */
void OLED_WriteSplitLine(uint8_t page, const char *left, const char *right); /* 15-char UI + 5-char fault sidebar */
#endif
