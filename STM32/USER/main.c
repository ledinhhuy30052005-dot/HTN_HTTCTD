#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "analog_input.h"
#include "relay.h"
#include "buzzer.h"
#include "app_config.h"
#include "taskN3.h"
#include "task3_standalone.h"

static void safe_fail(void)
{
    Relay_AllOff();
    Buzzer_Set(1U);
    Board_DelayMs(100U);
    Buzzer_Set(0U);
    NVIC_SystemReset();
    for (;;) { Relay_AllOff(); }
}

int main(void)
{
    /* Keep the same application origin as the group's bootloader layout. */
    SCB->VTOR = APP_START_ADDRESS;
    SystemCoreClockUpdate();

    Board_Init();
    if (!AnalogInput_Init()) safe_fail();
    Board_I2C1_Init();

    /* Create shared queues/configuration, then the safe harness and Task N3. */
    if (!TaskN3_InitShared()) safe_fail();
    if (Task3Standalone_Create() != pdPASS) safe_fail();
    if (TaskN3_Create() != pdPASS) safe_fail();

    vTaskStartScheduler();
    /* Scheduler returning indicates the kernel could not start safely. */
    Relay_AllOff();
    Buzzer_Set(0U);
    for (;;) { }
}
