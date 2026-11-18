//#include "stm32f10x.h"
//#include "FreeRTOS.h"
//#include "task.h"
//#include "board.h"
//#include "app_tasks.h"

//int main(void)
//{
//    /* The application is linked at 0x0800C800. This also protects interrupt
//     * vector lookup when the bootloader jumps to the application. VECT_TAB_OFFSET
//     * is defined in the Keil project as 0xC800 for SystemInit(). */
//    SCB->VTOR = 0x0800C800UL;
//    SystemCoreClockUpdate();

//    Board_Init();
//    Board_ADC_Init();
//    Board_I2C1_Init();
//    Board_UART1_Init(115200UL);

//    App_CreateTasks();
//    vTaskStartScheduler();

//    /* Reaching here means FreeRTOS could not allocate its idle task/heap. */
//    Board_AllRelaysOff();
//    for (;;) { Board_SetStatusLED(1U); }
//}


/* ------------------------------------------------------------------
 * main.c  -  CHUONG TRINH TEST LINH KIEN (khong dung FreeRTOS scheduler)
 *
 * Doc: do am dat (PA0), muc nuoc (PA2), DHT22 (PA3), BH1750 (I2C1)
 * Hien thi: OLED SSD1306 (I2C1) + in log qua USART1 (PA9, 115200)
 *
 * Dung lai driver san co trong USER/: board.c, i2c_bus.c, sensors.c,
 * dht22.c, bh1750.c, oled.c.  Thay the USER/main.c cu bang file nay
 * (giu ban cu lai thanh main_app.c.bak de sau nay khoi phuc).
 * ------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>
#include "stm32f10x.h"
#include "board.h"
#include "app_config.h"
#include "app_types.h"
#include "sensors.h"
#include "oled.h"

#define LOOP_MS          250U   /* chu ky vong lap chinh            */
#define DHT_EVERY_MS    2000U   /* DHT22 can >= 2 s giua 2 lan doc  */
#define LIGHT_EVERY_MS   500U   /* BH1750 H-res can ~180 ms         */
#define LOG_EVERY_MS    1000U

/* Delay ms dung TIM2 (1 MHz, do Board_Init cau hinh) - chinh xac hon
 * Board_DelayMs() vong lap NOP. */
static void delay_ms(uint32_t ms)
{
    while (ms-- != 0U) { Board_DelayUs(1000U); }
}

static void show(const SensorData_t *d, uint8_t dhtOk, uint32_t dhtFail,
                 uint32_t seconds)
{
    char line[24];

    OLED_WriteLine(0U, "SENSOR TEST     ");

    (void)snprintf(line, sizeof(line), "SOIL %4u %3u%%", d->soilRaw, d->soilPercent);
    OLED_WriteLine(1U, line);

    (void)snprintf(line, sizeof(line), "WATER %4u %s", d->waterRaw, d->waterOK ? "OK" : "LOW");
    OLED_WriteLine(2U, line);

    if (dhtOk)
        (void)snprintf(line, sizeof(line), "T %2dC  H %3u%%", (int)d->temperatureC, d->humidityPercent);
    else
        (void)snprintf(line, sizeof(line), "DHT22 ERROR     ");
    OLED_WriteLine(3U, line);

    if (d->lightValid)
        (void)snprintf(line, sizeof(line), "LUX %5u", d->lightLux);
    else
        (void)snprintf(line, sizeof(line), "BH1750 ERROR    ");
    OLED_WriteLine(4U, line);

    (void)snprintf(line, sizeof(line), "DHT FAIL %lu", (unsigned long)dhtFail);
    OLED_WriteLine(5U, line);

    OLED_WriteLine(6U, "                ");

    (void)snprintf(line, sizeof(line), "UP %lu S", (unsigned long)seconds);
    OLED_WriteLine(7U, line);
}

static void log_uart(const SensorData_t *d, uint8_t dhtOk)
{
    char out[96];
    (void)snprintf(out, sizeof(out),
        "SOIL raw=%u pct=%u | WATER raw=%u ok=%u | T=%d H=%u dht=%u | LUX=%u lux_ok=%u\r\n",
        d->soilRaw, d->soilPercent, d->waterRaw, d->waterOK,
        (int)d->temperatureC, d->humidityPercent, dhtOk,
        d->lightLux, d->lightValid);
    Board_UART1_SendString(out);
}

int main(void)
{
    SensorData_t data;
    uint32_t tMs = 0U;
    uint32_t lastDht = 0U, lastLight = 0U, lastLog = 0U;
    uint32_t dhtFail = 0U;
    uint8_t  dhtOk = 0U;

    /* SystemInit() (startup) da dat SCB->VTOR theo VECT_TAB_OFFSET. */
    SystemCoreClockUpdate();

    Board_Init();            /* GPIO, relay OFF, nut bam, DHT pin, TIM2 1 MHz */
    Board_ADC_Init();        /* ADC1 + DMA: PA0 (dat), PA2 (muc nuoc)        */
    Board_I2C1_Init();       /* I2C1 100 kHz PB6/PB7                         */
    Board_UART1_Init(115200UL);

    delay_ms(200U);          /* cho OLED / BH1750 len nguon on dinh          */
    OLED_Init();
    OLED_WriteLine(0U, "SENSOR TEST     ");
    OLED_WriteLine(2U, "STARTING...     ");

    Sensors_Init();          /* BH1750 power-on + continuous H-res           */
    delay_ms(200U);

    Board_UART1_SendString("\r\n=== SENSOR TEST START ===\r\n");

    memset(&data, 0, sizeof(data));
    for (;;)
    {
        Sensors_ReadFast(&data);                     /* soil + water (DMA)   */

        if ((tMs - lastLight) >= LIGHT_EVERY_MS)
        {
            lastLight = tMs;
            Sensors_ReadLight(&data);
            if (!data.lightValid) { Sensors_Init(); } /* thu khoi tao lai BH1750 */
        }

        if ((tMs >= DHT_EVERY_MS) && ((tMs - lastDht) >= DHT_EVERY_MS))
        {
            lastDht = tMs;
            Sensors_ReadDHT22(&data);
            dhtOk = data.dhtValid;
            if (!dhtOk) { dhtFail++; }
        }

        show(&data, dhtOk, dhtFail, tMs / 1000U);

        if ((tMs - lastLog) >= LOG_EVERY_MS)
        {
            lastLog = tMs;
            log_uart(&data, dhtOk);
        }

        Board_SetStatusLED((uint8_t)((tMs / 500U) & 1U)); /* LED PC13 nhap nhay = chuong trinh dang chay */
        delay_ms(LOOP_MS);
        tMs += LOOP_MS;   /* gan dung: chua tinh thoi gian ve OLED, thuc te cham hon mot chut */
    }
}

