#include "board.h"
#include "app_config.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_tim.h"

static volatile uint16_t s_adcBuffer[4];
static volatile uint8_t s_adcReady = 0U;

/* Relay module 5 V kich muc thap: dung open-drain. Muc "tat" = tha noi (module tu keo len),
 * khong de chan 3.3 V push-pull lam opto dan mo hoac relay nhay. Muc "bat" = keo xuong 0 V. */
static void gpio_output_od(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = pin;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(port, &g);
}

static void gpio_output(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = pin;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(port, &g);
}

void Board_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* Safe state first: all relay channels OFF before starting tasks. */
    GPIO_SetBits(GPIOB, GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
    gpio_output_od(GPIOB, GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
    gpio_output(GPIOB, GPIO_Pin_1); /* buzzer */
    Board_AllRelaysOff();
    Board_SetBuzzer(0U);

    /* Buttons: PA5=OK/MENU, PA6=UP, PA7=DOWN, PB0=BACK; active LOW. */
    g.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &g);
    g.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOB, &g);

    /* DHT11 on PA3; set to open-drain idle high. */
    g.GPIO_Pin = GPIO_Pin_3;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_Init(GPIOA, &g);
    GPIO_SetBits(GPIOA, GPIO_Pin_3);

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

uint8_t Board_ButtonPressed(uint8_t index)
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

static void relay_write(uint16_t pin, uint8_t on)
{
#if RELAY_ACTIVE_LOW
    if (on) GPIO_ResetBits(GPIOB, pin); else GPIO_SetBits(GPIOB, pin);
#else
    if (on) GPIO_SetBits(GPIOB, pin); else GPIO_ResetBits(GPIOB, pin);
#endif
}

/* Relay mapping from the latest wiring diagram (both 2-channel modules active LOW):
 * PB12 = IN1 = fan, PB13 = IN2 = lamp, PB14 = IN3 = water pump, PB15 = IN4 spare.
 */
void Board_SetFan(uint8_t on)   { relay_write(GPIO_Pin_12, on); }
void Board_SetLamp(uint8_t on)  { relay_write(GPIO_Pin_13, on); }
void Board_SetPump(uint8_t on)  { relay_write(GPIO_Pin_14, on); }
void Board_SetRelay4(uint8_t on){ relay_write(GPIO_Pin_15, on); }

void Board_AllRelaysOff(void)
{
    relay_write(GPIO_Pin_12, 0U);
    relay_write(GPIO_Pin_13, 0U);
    relay_write(GPIO_Pin_14, 0U);
    relay_write(GPIO_Pin_15, 0U);
}

void Board_SetBuzzer(uint8_t on)
{
#if BUZZER_ACTIVE_HIGH
    if (on) GPIO_SetBits(GPIOB, GPIO_Pin_1); else GPIO_ResetBits(GPIOB, GPIO_Pin_1);
#else
    if (on) GPIO_ResetBits(GPIOB, GPIO_Pin_1); else GPIO_SetBits(GPIOB, GPIO_Pin_1);
#endif
}

uint8_t Board_ADC_Init(void)
{
    GPIO_InitTypeDef g;
    ADC_InitTypeDef a;
    DMA_InitTypeDef d;
    uint32_t timeout;

    s_adcReady = 0U;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6); /* 72/6 = 12 MHz ADC clock */

    g.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_2;
    g.GPIO_Mode = GPIO_Mode_AIN;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &g);

    DMA_DeInit(DMA1_Channel1);
    DMA_StructInit(&d);
    d.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    d.DMA_MemoryBaseAddr = (uint32_t)s_adcBuffer;
    d.DMA_DIR = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize = 4U;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    d.DMA_Mode = DMA_Mode_Circular;
    d.DMA_Priority = DMA_Priority_High;
    d.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &d);
    DMA_Cmd(DMA1_Channel1, ENABLE);

    ADC_StructInit(&a);
    a.ADC_Mode = ADC_Mode_Independent;
    a.ADC_ScanConvMode = ENABLE;
    a.ADC_ContinuousConvMode = ENABLE;
    a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel = 4U;
    ADC_Init(ADC1, &a);
    /* Sample each analog probe twice consecutively. DMA readers use the SECOND
     * conversion of each pair, reducing residual sample-capacitor crosstalk when
     * the soil and water probes have different source impedances. */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1U, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 2U, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 3U, ADC_SampleTime_239Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 4U, ADC_SampleTime_239Cycles5);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    timeout = 1000000UL;
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET)
    {
        if (timeout-- == 0U) return 0U;
    }
    ADC_StartCalibration(ADC1);
    timeout = 1000000UL;
    while (ADC_GetCalibrationStatus(ADC1) != RESET)
    {
        if (timeout-- == 0U) return 0U;
    }
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    s_adcReady = 1U;
    return 1U;
}

uint8_t Board_ADC_IsReady(void) { return s_adcReady; }

uint8_t Board_ADC_Read(uint8_t index, uint16_t *value)
{
    if ((value == 0) || (index > 1U) || !s_adcReady) return 0U;
    /* Buffer ranks: PA0, PA0, PA2, PA2. Return the second conversion
     * for each input (stable sample after channel switching). */
    *value = s_adcBuffer[(index == 0U) ? 1U : 3U];
    return 1U;
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
