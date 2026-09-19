#include "digital_input.h"

#include "main.h"

#define DIGITAL_INPUT_COUNT 8U
#define DIGITAL_INPUT_DEBOUNCE_SAMPLES 3U

static const uint16_t s_pins[DIGITAL_INPUT_COUNT] = {
    DI1_Pin, DI2_Pin, DI3_Pin, DI4_Pin,
    DI5_Pin, DI6_Pin, DI7_Pin, DI8_Pin,
};

static const GPIO_TypeDef *s_ports[DIGITAL_INPUT_COUNT] = {
    DI1_GPIO_Port, DI2_GPIO_Port, DI3_GPIO_Port, DI4_GPIO_Port,
    DI5_GPIO_Port, DI6_GPIO_Port, DI7_GPIO_Port, DI8_GPIO_Port,
};

static uint8_t s_stable_bits;
static uint8_t s_last_bits;
static uint8_t s_counters[DIGITAL_INPUT_COUNT];

void DigitalInput_Init(void)
{
  uint8_t index;

  s_stable_bits = 0U;
  s_last_bits = 0U;

  for (index = 0U; index < DIGITAL_INPUT_COUNT; index++)
  {
    s_counters[index] = 0U;
  }
}

void DigitalInput_Update(void)
{
  uint8_t index;

  for (index = 0U; index < DIGITAL_INPUT_COUNT; index++)
  {
    uint8_t active = (HAL_GPIO_ReadPin((GPIO_TypeDef *)s_ports[index], s_pins[index]) == GPIO_PIN_RESET) ? 1U : 0U;

    if (active != ((s_last_bits >> index) & 0x01U))
    {
      s_counters[index] = 0U;
    }
    else if (s_counters[index] < DIGITAL_INPUT_DEBOUNCE_SAMPLES)
    {
      s_counters[index]++;
    }

    if (s_counters[index] >= DIGITAL_INPUT_DEBOUNCE_SAMPLES)
    {
      if (active != 0U)
      {
        s_stable_bits |= (uint8_t)(1U << index);
      }
      else
      {
        s_stable_bits &= (uint8_t)~(1U << index);
      }
    }

    s_last_bits = (uint8_t)((s_last_bits & ~(1U << index)) | (active << index));
  }
}

uint8_t DigitalInput_GetStableBits(void)
{
  return s_stable_bits;
}
