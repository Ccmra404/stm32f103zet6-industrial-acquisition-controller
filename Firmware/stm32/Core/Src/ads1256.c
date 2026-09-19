#include "ads1256.h"

#include "main.h"
#include "spi.h"

#define ADS1256_CMD_RDATA 0x01U
#define ADS1256_CMD_RREG 0x10U
#define ADS1256_CMD_WREG 0x50U
#define ADS1256_CMD_SDATAC 0x0FU
#define ADS1256_CMD_RESET 0xFEU
#define ADS1256_CMD_SYNC 0xFCU
#define ADS1256_CMD_WAKEUP 0xFFU

#define ADS1256_REG_STATUS 0x00U
#define ADS1256_REG_MUX 0x01U
#define ADS1256_REG_ADCON 0x02U
#define ADS1256_REG_DRATE 0x03U

static void ChipSelect(uint8_t selected)
{
  HAL_GPIO_WritePin(ADS1256_CS_GPIO_Port,
                    ADS1256_CS_Pin,
                    selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static uint8_t TransferByte(uint8_t value)
{
  uint8_t received = 0U;
  (void)HAL_SPI_TransmitReceive(&hspi2, &value, &received, 1U, 10U);
  return received;
}

static void SendCommand(uint8_t command)
{
  ChipSelect(1U);
  (void)TransferByte(command);
  ChipSelect(0U);
}

static void WriteRegister(uint8_t reg, uint8_t value)
{
  ChipSelect(1U);
  (void)TransferByte((uint8_t)(ADS1256_CMD_WREG | (reg & 0x0FU)));
  (void)TransferByte(0U);
  (void)TransferByte(value);
  ChipSelect(0U);
}

static uint8_t ReadRegister(uint8_t reg)
{
  uint8_t value;

  ChipSelect(1U);
  (void)TransferByte((uint8_t)(ADS1256_CMD_RREG | (reg & 0x0FU)));
  (void)TransferByte(0U);
  value = TransferByte(0xFFU);
  ChipSelect(0U);
  return value;
}

static uint8_t WaitReady(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  while (HAL_GPIO_ReadPin(ADS1256_DRDY_GPIO_Port, ADS1256_DRDY_Pin) != GPIO_PIN_RESET)
  {
    if ((HAL_GetTick() - start) >= timeout_ms)
    {
      return 0U;
    }
  }
  return 1U;
}

static uint8_t ReadData(int32_t *value)
{
  uint32_t raw;

  if (value == 0)
  {
    return 0U;
  }

  ChipSelect(1U);
  (void)TransferByte(ADS1256_CMD_RDATA);
  raw = ((uint32_t)TransferByte(0xFFU) << 16U);
  raw |= ((uint32_t)TransferByte(0xFFU) << 8U);
  raw |= (uint32_t)TransferByte(0xFFU);
  ChipSelect(0U);

  if ((raw & 0x00800000U) != 0U)
  {
    raw |= 0xFF000000U;
  }

  *value = (int32_t)raw;
  return 1U;
}

uint8_t ADS1256_Init(void)
{
  HAL_GPIO_WritePin(ADS1256_CS_GPIO_Port, ADS1256_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ADS1256_RESET_GPIO_Port, ADS1256_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(ADS1256_RESET_GPIO_Port, ADS1256_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);

  SendCommand(ADS1256_CMD_SDATAC);
  WriteRegister(ADS1256_REG_STATUS, 0x00U);
  WriteRegister(ADS1256_REG_MUX, 0x08U);
  WriteRegister(ADS1256_REG_ADCON, 0x20U);
  WriteRegister(ADS1256_REG_DRATE, 0x82U);
  HAL_Delay(10U);

  return (ReadRegister(ADS1256_REG_STATUS) == 0x00U) ? 1U : 0U;
}

uint8_t ADS1256_ReadChannel(uint8_t channel, int32_t *value)
{
  if ((channel > 7U) || (value == 0))
  {
    return 0U;
  }

  WriteRegister(ADS1256_REG_MUX, (uint8_t)(channel << 4U));
  SendCommand(ADS1256_CMD_SYNC);
  SendCommand(ADS1256_CMD_WAKEUP);

  if (WaitReady(200U) == 0U)
  {
    return 0U;
  }

  return ReadData(value);
}

uint8_t ADS1256_ReadAll(int32_t values[8])
{
  uint8_t channel;

  if (values == 0)
  {
    return 0U;
  }

  for (channel = 0U; channel < 8U; channel++)
  {
    if (ADS1256_ReadChannel(channel, &values[channel]) == 0U)
    {
      return 0U;
    }
  }

  return 1U;
}
