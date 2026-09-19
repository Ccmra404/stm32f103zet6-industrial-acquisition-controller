#ifndef DIGITAL_INPUT_H
#define DIGITAL_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void DigitalInput_Init(void);
void DigitalInput_Update(void);
uint8_t DigitalInput_GetStableBits(void);

#ifdef __cplusplus
}
#endif

#endif
