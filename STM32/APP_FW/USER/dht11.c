#include "dht11.h"
#include "board.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_tim.h"
#include "FreeRTOS.h"
#include "task.h"

#define DHT_PIN  GPIO_Pin_3
#define DHT_PORT GPIOA

static uint8_t wait_level(BitAction level, uint16_t timeoutUs)
{
    uint16_t start = (uint16_t)TIM_GetCounter(TIM2);
    uint32_t guard = ((uint32_t)timeoutUs * 40UL) + 100UL;
    while (GPIO_ReadInputDataBit(DHT_PORT, DHT_PIN) != level)
    {
        if ((uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start) >= timeoutUs) return 0U;
        /* Secondary guard: fail safely if TIM2 unexpectedly stops counting. */
        if (guard-- == 0U) return 0U;
    }
    return 1U;
}

static void dht_input_pullup(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = DHT_PIN;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(DHT_PORT, &g);
}

static void dht_output_od(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = DHT_PIN;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(DHT_PORT, &g);
}

/* DHT11 protocol: host holds DATA low for at least 18 ms; 40 bits contain
 * integer humidity/temperature and an 8-bit checksum. PA3 needs an external
 * 4.7k-10k pull-up to 3.3 V for reliable results. */
uint8_t DHT11_Read(int16_t *temperatureC, uint8_t *humidityPercent)
{
    uint8_t bytes[5] = {0U, 0U, 0U, 0U, 0U};
    uint8_t i;
    uint8_t j;
    uint16_t start;
    uint16_t highWidth;
    uint8_t sum;

    if ((temperatureC == 0) || (humidityPercent == 0)) return 0U;

    dht_output_od();
    /* The DHT11 start pulse must be at least 18 ms. Keep it in a bounded
     * busy wait; interrupts are still enabled during this start-low interval. */
    GPIO_ResetBits(DHT_PORT, DHT_PIN);
    for (j = 0U; j < 20U; j++) Board_DelayUs(1000U);

    /* Keep release-to-response and the data pulse measurement uninterrupted. */
    taskENTER_CRITICAL();
    GPIO_SetBits(DHT_PORT, DHT_PIN);   /* open-drain release */
    Board_DelayUs(30U);
    dht_input_pullup();

    if (!wait_level(Bit_RESET, 100U)) { taskEXIT_CRITICAL(); return 0U; }
    if (!wait_level(Bit_SET,   100U)) { taskEXIT_CRITICAL(); return 0U; }
    if (!wait_level(Bit_RESET, 100U)) { taskEXIT_CRITICAL(); return 0U; }

    for (i = 0U; i < 40U; i++)
    {
        if (!wait_level(Bit_SET, 100U)) { taskEXIT_CRITICAL(); return 0U; }
        start = (uint16_t)TIM_GetCounter(TIM2);
        {
            uint32_t guard = 4100UL;
            while (GPIO_ReadInputDataBit(DHT_PORT, DHT_PIN) == Bit_SET)
            {
                if ((uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start) > 100U) break;
                if (guard-- == 0U) break;
            }
        }
        highWidth = (uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start);
        bytes[i / 8U] <<= 1;
        if (highWidth > 40U) bytes[i / 8U] |= 1U;
        if (!wait_level(Bit_RESET, 100U)) { taskEXIT_CRITICAL(); return 0U; }
    }
    taskEXIT_CRITICAL();

    sum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
    if (sum != bytes[4]) return 0U;

    /* DHT11 byte 0 = integer RH, byte 2 = integer temperature. */
    *humidityPercent = bytes[0];
    *temperatureC = (int16_t)(bytes[2] & 0x7FU);
    if (bytes[2] & 0x80U) *temperatureC = (int16_t)-(*temperatureC);
    if (*humidityPercent > 100U || *temperatureC < -10 || *temperatureC > 60) return 0U;
    return 1U;
}
