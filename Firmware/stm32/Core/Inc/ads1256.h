#ifndef ADS1256_H
#define ADS1256_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t ADS1256_Init(void);
uint8_t ADS1256_ReadChannel(uint8_t channel, int32_t *value);
uint8_t ADS1256_ReadAll(int32_t values[8]);

#ifdef __cplusplus
}
#endif

#endif
