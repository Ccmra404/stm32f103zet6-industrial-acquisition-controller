#include "max31865.h"

#include "config_store.h"
#include "main.h"
#include "spi.h"

#define MAX31865_REG_CONFIG 0x00U
#define MAX31865_REG_RTD_MSB 0x01U
#define MAX31865_REG_RTD_LSB 0x02U

#define MAX31865_CONFIG_50HZ 0x01U
#define MAX31865_CONFIG_FAULT_CLEAR 0x02U
#define MAX31865_CONFIG_3WIRE 0x10U
#define MAX31865_CONFIG_AUTO 0x40U
#define MAX31865_CONFIG_VBIAS 0x80U

static uint32_t s_rref_milliohm = 430000U;
static uint32_t s_r0_milliohm = 100000U;

static void ChipSelect(uint8_t selected)
{
  HAL_GPIO_WritePin(MAX31865_CS_GPIO_Port,
                    MAX31865_CS_Pin,
                    selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static uint8_t TransferByte(uint8_t value)
{
  uint8_t received = 0U;
  (void)HAL_SPI_TransmitReceive(&hspi3, &value, &received, 1U, 10U);
  return received;
}

static void WriteRegister(uint8_t reg, uint8_t value)
{
  ChipSelect(1U);
  (void)TransferByte((uint8_t)(reg & 0x7FU));
  (void)TransferByte(value);
  ChipSelect(0U);
}

static uint8_t ReadRegister(uint8_t reg)
{
  uint8_t value;

  ChipSelect(1U);
  (void)TransferByte((uint8_t)(reg | 0x80U));
  value = TransferByte(0xFFU);
  ChipSelect(0U);
  return value;
}

void MAX31865_Init(void)
{
  const DeviceConfig *config = ConfigStore_Get();

  if ((config != 0) &&
      (config->rtd_rref_milliohm > 0U) &&
      (config->rtd_r0_milliohm > 0U))
  {
    s_rref_milliohm = config->rtd_rref_milliohm;
    s_r0_milliohm = config->rtd_r0_milliohm;
  }

  WriteRegister(MAX31865_REG_CONFIG,
                MAX31865_CONFIG_VBIAS |
                MAX31865_CONFIG_AUTO |
                MAX31865_CONFIG_3WIRE |
                MAX31865_CONFIG_50HZ);
  WriteRegister(MAX31865_REG_CONFIG,
                MAX31865_CONFIG_VBIAS |
                MAX31865_CONFIG_AUTO |
                MAX31865_CONFIG_3WIRE |
                MAX31865_CONFIG_50HZ |
                MAX31865_CONFIG_FAULT_CLEAR);
  HAL_Delay(70U);
}

uint8_t MAX31865_ReadMilliCelsius(int32_t *temperature_millicelsius)
{
  uint16_t raw;
  uint32_t resistance_milliohm;
  int32_t delta_milliohm;

  if (temperature_millicelsius == 0)
  {
    return 0U;
  }

  raw = (uint16_t)ReadRegister(MAX31865_REG_RTD_MSB) << 8U;
  raw |= ReadRegister(MAX31865_REG_RTD_LSB);

  if ((raw & 0x0001U) != 0U)
  {
    return 0U;
  }

  raw >>= 1U;
  resistance_milliohm = (uint32_t)(((uint64_t)s_rref_milliohm * raw) / 32768U);
  delta_milliohm = (int32_t)resistance_milliohm - (int32_t)s_r0_milliohm;

  *temperature_millicelsius = (delta_milliohm * 1000) / 385;
  return 1U;
}
