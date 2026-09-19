#ifndef ANALOG_OUTPUT_H
#define ANALOG_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void AnalogOutput_Init(void);
uint8_t AnalogOutput_SetRaw(uint8_t channel, uint16_t value);
uint16_t AnalogOutput_GetRaw(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif
