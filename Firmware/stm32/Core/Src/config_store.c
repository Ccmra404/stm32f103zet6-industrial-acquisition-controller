#include "config_store.h"

#include "bridge_protocol.h"
#include "i2c.h"
#include "main.h"
#include <string.h>

#define CONFIG_MAGIC 0x43464731U
#define CONFIG_VERSION 1U
#define CONFIG_EEPROM_ADDRESS 0xA0U
#define CONFIG_STORAGE_ADDRESS 0x0000U

static DeviceConfig s_config;

static uint16_t ConfigCrc(const DeviceConfig *config)
{
  return BridgeProtocol_Crc16((const uint8_t *)config,
                              (uint16_t)(sizeof(DeviceConfig) - sizeof(uint16_t)));
}

static void ConfigSetDefaults(void)
{
  memset(&s_config, 0, sizeof(s_config));
  s_config.magic = CONFIG_MAGIC;
  s_config.version = CONFIG_VERSION;
  s_config.rtd_rref_milliohm = 430000U;
  s_config.rtd_r0_milliohm = 100000U;
  s_config.relay_safe_mask = 0U;
  s_config.crc = ConfigCrc(&s_config);
}

void ConfigStore_Init(void)
{
  ConfigSetDefaults();
  (void)ConfigStore_Load();
}

uint8_t ConfigStore_Load(void)
{
  DeviceConfig stored;

  if (HAL_I2C_Mem_Read(&hi2c2,
                       CONFIG_EEPROM_ADDRESS,
                       CONFIG_STORAGE_ADDRESS,
                       I2C_MEMADD_SIZE_16BIT,
                       (uint8_t *)&stored,
                       sizeof(stored),
                       100U) != HAL_OK)
  {
    ConfigSetDefaults();
    return 0U;
  }

  if ((stored.magic != CONFIG_MAGIC) ||
      (stored.version != CONFIG_VERSION) ||
      (stored.crc != ConfigCrc(&stored)))
  {
    ConfigSetDefaults();
    return 0U;
  }

  s_config = stored;
  return 1U;
}

uint8_t ConfigStore_Save(void)
{
  s_config.crc = ConfigCrc(&s_config);
  return (HAL_I2C_Mem_Write(&hi2c2,
                            CONFIG_EEPROM_ADDRESS,
                            CONFIG_STORAGE_ADDRESS,
                            I2C_MEMADD_SIZE_16BIT,
                            (uint8_t *)&s_config,
                            sizeof(s_config),
                            100U) == HAL_OK) ? 1U : 0U;
}

const DeviceConfig *ConfigStore_Get(void)
{
  return &s_config;
}
