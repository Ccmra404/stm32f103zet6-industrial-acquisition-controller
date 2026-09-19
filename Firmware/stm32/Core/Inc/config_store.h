#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint32_t rtd_rref_milliohm;
  uint32_t rtd_r0_milliohm;
  uint8_t relay_safe_mask;
  uint16_t crc;
} DeviceConfig;

void ConfigStore_Init(void);
uint8_t ConfigStore_Load(void);
uint8_t ConfigStore_Save(void);
const DeviceConfig *ConfigStore_Get(void);

#ifdef __cplusplus
}
#endif

#endif
