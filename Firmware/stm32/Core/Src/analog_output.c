#include "analog_output.h"

#include "dac.h"
#include "device_state.h"

#define DAC_MAX_VALUE 4095U

void AnalogOutput_Init(void)
{
  (void)HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
  (void)HAL_DAC_Start(&hdac, DAC_CHANNEL_2);
  (void)AnalogOutput_SetRaw(0U, 0U);
  (void)AnalogOutput_SetRaw(1U, 0U);
}

uint8_t AnalogOutput_SetRaw(uint8_t channel, uint16_t value)
{
  uint32_t dac_channel;

  if (channel > 1U)
  {
    return 0U;
  }

  if (value > DAC_MAX_VALUE)
  {
    value = DAC_MAX_VALUE;
  }

  dac_channel = (channel == 0U) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
  if (HAL_DAC_SetValue(&hdac, dac_channel, DAC_ALIGN_12B_R, value) != HAL_OK)
  {
    return 0U;
  }

  DeviceState_UpdateAnalogOutput(channel, value);
  return 1U;
}

uint16_t AnalogOutput_GetRaw(uint8_t channel)
{
  DeviceStateSnapshot state = DeviceState_Get();
  return (channel < 2U) ? state.analog_output_raw[channel] : 0U;
}
