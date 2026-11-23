#include "dht11.h"
#include "board.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_tim.h"
#include "FreeRTOS.h"
#include "task.h"

#define DHT11_PIN  GPIO_Pin_3
#define DHT11_PORT GPIOA

static void dht11_output_open_drain(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DHT11_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(DHT11_PORT, &gpio);
}

static void dht11_input_pullup(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = DHT11_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(DHT11_PORT, &gpio);
}

static uint8_t wait_pin(BitAction level, uint16_t timeoutUs)
{
    uint16_t start = (uint16_t)TIM_GetCounter(TIM2);
    while (GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) != level)
    {
        if ((uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start) >= timeoutUs)
            return 0U;
    }
    return 1U;
}

uint8_t DHT11_Read(int16_t *temperatureC, uint8_t *humidityPercent)
{
    uint8_t bytes[5] = {0U, 0U, 0U, 0U, 0U};
    uint8_t i;
    uint16_t pulseWidth;
    uint16_t start;
    uint8_t checksum;

    if ((temperatureC == 0) || (humidityPercent == 0)) return 0U;

    /* Host start signal: pull DATA low for at least 18 ms. */
    dht11_output_open_drain();
    GPIO_ResetBits(DHT11_PORT, DHT11_PIN);
    vTaskDelay(pdMS_TO_TICKS(20U));
    GPIO_SetBits(DHT11_PORT, DHT11_PIN);
    Board_DelayUs(30U);
    dht11_input_pullup();

    /* The response and 40 data bits are timing-critical. */
    taskENTER_CRITICAL();
    if (!wait_pin(Bit_RESET, 100U) ||
        !wait_pin(Bit_SET, 100U) ||
        !wait_pin(Bit_RESET, 100U))
    {
        taskEXIT_CRITICAL();
        return 0U;
    }

    for (i = 0U; i < 40U; i++)
    {
        if (!wait_pin(Bit_SET, 100U))
        {
            taskEXIT_CRITICAL();
            return 0U;
        }

        start = (uint16_t)TIM_GetCounter(TIM2);
        while (GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) == Bit_SET)
        {
            if ((uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start) > 100U)
                break;
        }
        pulseWidth = (uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start);

        bytes[i / 8U] <<= 1;
        if (pulseWidth > 40U) bytes[i / 8U] |= 1U;

        if (!wait_pin(Bit_RESET, 100U))
        {
            taskEXIT_CRITICAL();
            return 0U;
        }
    }
    taskEXIT_CRITICAL();

    checksum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
    if (checksum != bytes[4]) return 0U;

    /* DHT11: integer humidity and temperature; byte 1/3 are decimal parts. */
    *humidityPercent = bytes[0];
    *temperatureC = (int16_t)bytes[2];

    if (*humidityPercent > 100U || *temperatureC > 80) return 0U;
    return 1U;
}
