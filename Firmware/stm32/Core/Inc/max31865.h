#ifndef MAX31865_H
#define MAX31865_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void MAX31865_Init(void);
uint8_t MAX31865_ReadMilliCelsius(int32_t *temperature_millicelsius);

#ifdef __cplusplus
}
#endif

#endif
