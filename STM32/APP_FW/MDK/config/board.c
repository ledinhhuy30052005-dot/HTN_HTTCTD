#include "board.h"
#include "app_config.h"
#include "button.h"
#include "relay.h"
#include "buzzer.h"
#include "dht11.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_tim.h"

/* Relay module 5 V kich muc thap: dung open-drain. Muc "tat" = tha noi (module tu keo len),
 * khong de chan 3.3 V push-pull lam opto dan mo hoac relay nhay. Muc "bat" = keo xuong 0 V. */
void Board_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* Driver-specific GPIO setup is kept in each peripheral module. */
    Relay_Init();
    Buzzer_Init();
    Button_Init();
    DHT11_Init();

    SystemCoreClockUpdate();
    Board_TIM2_Init1MHz();
}


void Board_TIM2_Init1MHz(void)
{
    TIM_TimeBaseInitTypeDef t;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseStructInit(&t);
    t.TIM_Prescaler = (uint16_t)((SystemCoreClock / 1000000UL) - 1UL);
    t.TIM_CounterMode = TIM_CounterMode_Up;
    t.TIM_Period = 0xFFFFU;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM2, &t);
    TIM_SetCounter(TIM2, 0U);
    TIM_Cmd(TIM2, ENABLE);
}

void Board_DelayUs(uint16_t us)
{
    uint16_t start = (uint16_t)TIM_GetCounter(TIM2);
    uint32_t guard = ((uint32_t)us * 40UL) + 100UL;
    while ((uint16_t)((uint16_t)TIM_GetCounter(TIM2) - start) < us)
    {
        /* Fail boundedly if TIM2 is not advancing; sensor read will then fail safely. */
        if (guard-- == 0U) break;
    }
}

void Board_DelayMs(uint32_t ms)
{
    /* Only used before scheduler startup. During operation use vTaskDelay(). */
    volatile uint32_t i;
    while (ms-- != 0U)
    {
        for (i = 0U; i < (SystemCoreClock / 8000U); i++) { __NOP(); }
    }
}



/* Neu MCU reset giua mot giao dich, OLED/DS3231 co the giu SDA o muc thap va I2C1 bao BUSY mai.
 * Gia lap 9 xung SCL + dieu kien STOP de slave nha bus. Can TIM2 (Board_DelayUs) dang chay. */
static void i2c1_unstick(void)
{
    GPIO_InitTypeDef g;
    uint8_t n;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOB, &g);
    GPIO_SetBits(GPIOB, GPIO_Pin_6 | GPIO_Pin_7);
    Board_DelayUs(10U);

    for (n = 0U; n < 9U; n++)
    {
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7) != Bit_RESET) break; /* SDA da duoc nha */
        GPIO_ResetBits(GPIOB, GPIO_Pin_6);
        Board_DelayUs(5U);
        GPIO_SetBits(GPIOB, GPIO_Pin_6);
        Board_DelayUs(5U);
    }
    /* STOP: SDA 0 -> 1 khi SCL = 1 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_7);
    Board_DelayUs(5U);
    GPIO_SetBits(GPIOB, GPIO_Pin_6);
    Board_DelayUs(5U);
    GPIO_SetBits(GPIOB, GPIO_Pin_7);
    Board_DelayUs(5U);
}

void Board_I2C1_Recover(void)
{
    I2C_Cmd(I2C1, DISABLE);
    Board_I2C1_Init();
}

void Board_I2C1_Init(void)
{
    GPIO_InitTypeDef g;
    I2C_InitTypeDef i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    i2c1_unstick();

    g.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &g);

    I2C_DeInit(I2C1);
    I2C_StructInit(&i);
    i.I2C_ClockSpeed = 100000U;
    i.I2C_Mode = I2C_Mode_I2C;
    i.I2C_DutyCycle = I2C_DutyCycle_2;
    i.I2C_OwnAddress1 = 0x00U;
    i.I2C_Ack = I2C_Ack_Enable;
    i.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &i);
    I2C_Cmd(I2C1, ENABLE);
}
