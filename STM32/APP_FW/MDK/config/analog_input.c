#include "analog_input.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_dma.h"

static volatile uint16_t s_adcBuffer[4];
static volatile uint8_t s_adcReady = 0U;

uint8_t AnalogInput_Init(void)
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

uint8_t AnalogInput_IsReady(void) { return s_adcReady; }

uint8_t AnalogInput_Read(uint8_t index, uint16_t *value)
{
    if ((value == 0) || (index > 1U) || !s_adcReady) return 0U;
    /* Buffer ranks: PA0, PA0, PA2, PA2. Return the second conversion
     * for each input (stable sample after channel switching). */
    *value = s_adcBuffer[(index == 0U) ? 1U : 3U];
    return 1U;
}
