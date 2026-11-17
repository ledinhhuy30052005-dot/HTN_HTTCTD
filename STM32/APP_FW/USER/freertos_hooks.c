#include "FreeRTOS.h"
#include "task.h"
#include "board.h"
#include "stm32f10x.h"

/* On stack overflow or allocation failure, keep all actuators safe and recover by
 * system reset rather than remaining forever in an interrupt-disabled dead loop. */
static void fault_safe_reset(void)
{
    Board_AllRelaysOff();
    Board_SetBuzzer(1U);
    Board_DelayMs(100U);
    Board_SetBuzzer(0U);
    NVIC_SystemReset();
    for (;;) { Board_AllRelaysOff(); }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    fault_safe_reset();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *taskName)
{
    (void)task;
    (void)taskName;
    taskDISABLE_INTERRUPTS();
    fault_safe_reset();
}
