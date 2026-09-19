#include "relay_output.h"

#include "main.h"

#define RELAY_COUNT 8U

static const uint16_t s_pins[RELAY_COUNT] = {
    RELAY1_Pin, RELAY2_Pin, RELAY3_Pin, RELAY4_Pin,
    RELAY5_Pin, RELAY6_Pin, RELAY7_Pin, RELAY8_Pin,
};

static uint8_t s_relay_mask;

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

  s_relay_mask = mask;
}

uint8_t RelayOutput_GetMask(void)
{
  return s_relay_mask;
}

void RelayOutput_AllOff(void)
{
  RelayOutput_SetMask(0U);
}
