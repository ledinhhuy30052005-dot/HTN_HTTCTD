#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "analog_input.h"
#include "relay.h"
#include "buzzer.h"
#include "taskN1.h"
#include "taskN2.h"
#include "taskN3.h"

int main(void)
{
    /* Application is linked at 0x0800C800. Bootloader is kept at 0x08000000. */
    SCB->VTOR = 0x0800C800UL;
    SystemCoreClockUpdate();

    Board_Init();
    if (!AnalogInput_Init())
    {
        Relay_AllOff();
        Buzzer_Set(1U);
        Board_DelayMs(100U);
        Buzzer_Set(0U);
        NVIC_SystemReset();
        for (;;) { Relay_AllOff(); }
    }
    Board_I2C1_Init();

    /* Shared queues/mutexes/configuration are ready before task creation. */
    if (!TaskN3_InitShared() ||
        TaskN1_Create() != pdPASS ||
        TaskN2_Create() != pdPASS ||
        TaskN3_Create() != pdPASS)
    {
        Relay_AllOff();
        Buzzer_Set(1U);
        Board_DelayMs(100U);
        Buzzer_Set(0U);
        NVIC_SystemReset();
        for (;;) { Relay_AllOff(); }
    }

    vTaskStartScheduler();
    /* Reaching here means FreeRTOS could not allocate its idle task/heap. */
    Relay_AllOff();
    Buzzer_Set(0U);
    for (;;) { }
}
