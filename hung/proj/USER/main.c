#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "app_tasks.h"

int main(void)
{
    /* Application vector table follows the existing bootloader layout. */
    SCB->VTOR = 0x0800C800UL;
    SystemCoreClockUpdate();

    Board_Init();          /* GPIO + TIM2 (1 MHz) for DHT11 pulse timing */
    Board_I2C1_Init();      /* BH1750 and OLED */
    App_CreateTasks();
    vTaskStartScheduler();

    /* Reaching here means FreeRTOS could not create its idle task/heap. */
    Board_AllRelaysOff();
    for (;;) { Board_SetStatusLED(1U); }
}
