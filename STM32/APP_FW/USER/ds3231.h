#ifndef DS3231_H
#define DS3231_H
#include <stdint.h>
typedef struct {
		uint8_t second, 
		uint8_t	minute, 
		uint8_t	hour, 
		uint8_t	day, 
		uint8_t	month; 
		uint16_t year; 
} DS3231_Time_t;
uint8_t DS3231_ReadTime(DS3231_Time_t *t);
uint8_t DS3231_SetTime(const DS3231_Time_t *t);
#endif
