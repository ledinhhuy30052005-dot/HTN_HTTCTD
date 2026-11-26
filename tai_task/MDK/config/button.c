#include "button.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"

void Button_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &g);
    g.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOB, &g);
}

uint8_t Button_IsPressed(uint8_t index)
{
    uint8_t state = 1U;
    switch (index)
    {
        case 0U: state = (uint8_t)GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5); break; /* OK */
        case 1U: state = (uint8_t)GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_6); break; /* UP */
        case 2U: state = (uint8_t)GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_7); break; /* DOWN */
        default: state = (uint8_t)GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0); break; /* BACK */
    }
    return (state == 0U) ? 1U : 0U;
}
