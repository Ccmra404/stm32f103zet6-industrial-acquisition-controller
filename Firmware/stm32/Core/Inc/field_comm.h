#ifndef FIELD_COMM_H
#define FIELD_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t FieldComm_Init(void);
uint8_t FieldComm_SendRs485(const uint8_t *data, uint16_t length, uint32_t timeout);
uint8_t FieldComm_SendRs232(const uint8_t *data, uint16_t length, uint32_t timeout);
uint8_t FieldComm_SendCan(uint16_t identifier, const uint8_t *data, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif
