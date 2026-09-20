#include "relay_output.h"

#include "main.h"

#define RELAY_COUNT 8U

static const uint16_t s_pins[RELAY_COUNT] = {
    RELAY1_Pin, RELAY2_Pin, RELAY3_Pin, RELAY4_Pin,
    RELAY5_Pin, RELAY6_Pin, RELAY7_Pin, RELAY8_Pin,
};

void RelayOutput_Init(void)
{
  RelayOutput_AllOff();
}

void RelayOutput_SetMask(uint8_t mask)
{
  uint8_t index;

  for (index = 0U; index < RELAY_COUNT; index++)
  {
    GPIO_PinState state = ((mask >> index) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(GPIOD, s_pins[index], state);
  }
}

uint8_t RelayOutput_GetMask(void)
{
  uint8_t mask = 0U;

  for (uint8_t index = 0U; index < RELAY_COUNT; index++)
  {
    if (HAL_GPIO_ReadPin(GPIOD, s_pins[index]) == GPIO_PIN_SET)
    {
      mask |= (uint8_t)(1U << index);
    }
  }

  return mask;
}

void RelayOutput_AllOff(void)
{
  RelayOutput_SetMask(0U);
}
