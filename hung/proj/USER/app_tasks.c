#include "app_tasks.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"
#include "i2c_bus.h"
#include "bh1750.h"
#include "dht11.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
    uint16_t lux;
    int16_t temperatureC;
    uint8_t humidity;
    uint8_t lightValid;
    uint8_t dhtValid;
} DisplayData_t;

static QueueHandle_t s_displayQueue;
static SemaphoreHandle_t s_i2cMutex;

static void SensorTask(void *argument);
static void DisplayTask(void *argument);

void App_CreateTasks(void)
{
    s_displayQueue = xQueueCreate(1U, sizeof(DisplayData_t));
    s_i2cMutex = xSemaphoreCreateMutex();

    if ((s_displayQueue == NULL) || (s_i2cMutex == NULL))
    {
        Board_AllRelaysOff();
        for (;;) { Board_SetStatusLED(1U); vTaskDelay(pdMS_TO_TICKS(200U)); Board_SetStatusLED(0U); vTaskDelay(pdMS_TO_TICKS(200U)); }
    }

    I2C1_SetMutex(s_i2cMutex);

    if (xTaskCreate(SensorTask, "Sensor", 256U, NULL, 2U, NULL) != pdPASS ||
        xTaskCreate(DisplayTask, "OLED", 256U, NULL, 1U, NULL) != pdPASS)
    {
        Board_AllRelaysOff();
        for (;;) { Board_SetStatusLED(1U); vTaskDelay(pdMS_TO_TICKS(200U)); Board_SetStatusLED(0U); vTaskDelay(pdMS_TO_TICKS(200U)); }
    }
}

static void SensorTask(void *argument)
{
    DisplayData_t data;
    uint8_t lightReady;
    TickType_t lastWake = xTaskGetTickCount();
    (void)argument;
    memset(&data, 0, sizeof(data));

    /* BH1750 and OLED share I2C1; the I2C driver serializes bus transactions. */
    vTaskDelay(pdMS_TO_TICKS(100U));
    lightReady = BH1750_Init();

    for (;;)
    {
        uint16_t lux;
        int16_t temperature;
        uint8_t humidity;

        if (lightReady && BH1750_ReadLux(&lux))
        {
            data.lux = lux;
            data.lightValid = 1U;
        }
        else
        {
            data.lightValid = 0U;
            /* Try to recover if the light sensor was not ready at startup. */
            lightReady = BH1750_Init();
        }

        if (DHT11_Read(&temperature, &humidity))
        {
            data.temperatureC = temperature;
            data.humidity = humidity;
            data.dhtValid = 1U;
        }
        else
        {
            data.dhtValid = 0U;
        }

        (void)xQueueOverwrite(s_displayQueue, &data);
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(2000U));
    }
}

static void DisplayTask(void *argument)
{
    DisplayData_t data;
    char line[20];
    TickType_t lastWake = xTaskGetTickCount();
    (void)argument;
    memset(&data, 0, sizeof(data));

    OLED_Init();
    OLED_WriteLine(0U, "SENSOR MONITOR  ");

    for (;;)
    {
        (void)xQueuePeek(s_displayQueue, &data, 0U);

        OLED_WriteLine(0U, "SENSOR MONITOR  ");

        if (data.lightValid)
            (void)snprintf(line, sizeof(line), "LIGHT: %4u LUX", data.lux);
        else
            (void)snprintf(line, sizeof(line), "LIGHT: SENSOR ERR");
        OLED_WriteLine(2U, line);

        if (data.dhtValid)
        {
            (void)snprintf(line, sizeof(line), "TEMP:  %3d C", (int)data.temperatureC);
            OLED_WriteLine(4U, line);
            (void)snprintf(line, sizeof(line), "HUM:   %3u %%", data.humidity);
            OLED_WriteLine(5U, line);
            OLED_WriteLine(7U, "DHT11: OK       ");
        }
        else
        {
            OLED_WriteLine(4U, "TEMP:  -- C     ");
            OLED_WriteLine(5U, "HUM:   -- %     ");
            OLED_WriteLine(7U, "DHT11: ERROR    ");
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500U));
    }
}
