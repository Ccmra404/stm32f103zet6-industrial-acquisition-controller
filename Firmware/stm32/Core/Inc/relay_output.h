#ifndef RELAY_OUTPUT_H
#define RELAY_OUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void RelayOutput_Init(void);
void RelayOutput_SetMask(uint8_t mask);
uint8_t RelayOutput_GetMask(void);
void RelayOutput_AllOff(void);

#ifdef __cplusplus
}
#endif

#endif
