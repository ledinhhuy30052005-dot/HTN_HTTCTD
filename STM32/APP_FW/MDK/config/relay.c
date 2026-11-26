#include "relay.h"
#include "app_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"

static void relay_write(uint16_t pin, uint8_t on)
{
#if RELAY_ACTIVE_LOW
    if (on) GPIO_ResetBits(GPIOB, pin); else GPIO_SetBits(GPIOB, pin);
#else
    if (on) GPIO_SetBits(GPIOB, pin); else GPIO_ResetBits(GPIOB, pin);
#endif
}

void Relay_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    /* Set inactive level before switching to open-drain output. */
    GPIO_SetBits(GPIOB, GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
    g.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOB, &g);
    Relay_AllOff();
}

/* Relay map: PB12=fan, PB13=lamp, PB14=pump, PB15=spare. Active LOW. */
void Relay_SetFan(uint8_t on)    { relay_write(GPIO_Pin_12, on); }
void Relay_SetLamp(uint8_t on)   { relay_write(GPIO_Pin_13, on); }
void Relay_SetPump(uint8_t on)   { relay_write(GPIO_Pin_14, on); }
void Relay_SetSpare(uint8_t on)  { relay_write(GPIO_Pin_15, on); }
void Relay_AllOff(void)
{
    Relay_SetFan(0U);
    Relay_SetLamp(0U);
    Relay_SetPump(0U);
    Relay_SetSpare(0U);
}
