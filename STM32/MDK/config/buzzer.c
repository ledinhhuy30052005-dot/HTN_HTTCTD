#include "buzzer.h"
#include "app_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"

void Buzzer_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = GPIO_Pin_1;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &g);
    Buzzer_Set(0U);
}

void Buzzer_Set(uint8_t on)
{
#if BUZZER_ACTIVE_HIGH
    if (on) GPIO_SetBits(GPIOB, GPIO_Pin_1); else GPIO_ResetBits(GPIOB, GPIO_Pin_1);
#else
    if (on) GPIO_ResetBits(GPIOB, GPIO_Pin_1); else GPIO_SetBits(GPIOB, GPIO_Pin_1);
#endif
}
