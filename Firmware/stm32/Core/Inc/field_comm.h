#ifndef FIELD_COMM_H
#define FIELD_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#include <stdint.h>

#define FIELD_COMM_RS232_RX_BUFFER_SIZE 256U
#define FIELD_COMM_CAN_RX_DEPTH 16U
#define FIELD_COMM_CAN_MAX_DATA 8U

typedef struct
{
  uint32_t identifier;
  uint8_t length;
  uint8_t data[FIELD_COMM_CAN_MAX_DATA];
} FieldCommCanFrame;

uint8_t FieldComm_Init(void);
uint8_t FieldComm_ReadRs485Byte(uint8_t *byte, uint32_t timeout);
uint8_t FieldComm_SendRs485(const uint8_t *data, uint16_t length, uint32_t timeout);
uint8_t FieldComm_SendRs232(const uint8_t *data, uint16_t length, uint32_t timeout);
uint8_t FieldComm_SendCan(uint16_t identifier, const uint8_t *data, uint8_t length);

/* RS232 (UART4) receive: interrupt driven, drained by the bridge task. */
uint8_t FieldComm_ReadRs232Byte(uint8_t *byte, uint32_t timeout);
uint16_t FieldComm_ReadRs232Bytes(uint8_t *buffer, uint16_t max_length);
uint16_t FieldComm_Rs232Available(void);

/* CAN receive: FIFO0 notification copied into a small software queue. */
uint8_t FieldComm_ReadCanFrame(FieldCommCanFrame *frame, uint32_t timeout);
uint16_t FieldComm_CanAvailable(void);

uint32_t FieldComm_Rs232RxCount(void);
uint32_t FieldComm_CanRxCount(void);
uint32_t FieldComm_Rs232Dropped(void);
uint32_t FieldComm_CanDropped(void);

/* Called from the shared HAL UART error callback. */
void FieldComm_OnUartError(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif
