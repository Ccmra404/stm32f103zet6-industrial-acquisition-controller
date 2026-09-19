#include "field_comm.h"

#include "can.h"
#include "main.h"
#include "usart.h"

uint8_t FieldComm_Init(void)
{
  CAN_FilterTypeDef filter = {0};
  uint32_t mailboxes;

  filter.FilterIdHigh = 0U;
  filter.FilterIdLow = 0U;
  filter.FilterMaskIdHigh = 0U;
  filter.FilterMaskIdLow = 0U;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = 14U;

  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port, RS485_DIR_Pin, GPIO_PIN_RESET);

  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_CAN_Start(&hcan) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
  {
    return 0U;
  }

  (void)mailboxes;
  return 1U;
}

uint8_t FieldComm_SendRs485(const uint8_t *data, uint16_t length, uint32_t timeout)
{
  HAL_StatusTypeDef status;
  uint32_t start;

  if ((data == 0) || (length == 0U))
  {
    return 0U;
  }

  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port, RS485_DIR_Pin, GPIO_PIN_SET);
  status = HAL_UART_Transmit(&huart2, (uint8_t *)data, length, timeout);

  start = HAL_GetTick();
  while ((__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) &&
         ((HAL_GetTick() - start) < timeout))
  {
  }

  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port, RS485_DIR_Pin, GPIO_PIN_RESET);
  return (status == HAL_OK) ? 1U : 0U;
}

uint8_t FieldComm_SendRs232(const uint8_t *data, uint16_t length, uint32_t timeout)
{
  if ((data == 0) || (length == 0U))
  {
    return 0U;
  }

  return (HAL_UART_Transmit(&huart4, (uint8_t *)data, length, timeout) == HAL_OK) ? 1U : 0U;
}

uint8_t FieldComm_SendCan(uint16_t identifier, const uint8_t *data, uint8_t length)
{
  CAN_TxHeaderTypeDef header = {0};
  uint32_t mailbox;

  if ((data == 0) || (length > 8U))
  {
    return 0U;
  }

  header.StdId = identifier;
  header.IDE = CAN_ID_STD;
  header.RTR = CAN_RTR_DATA;
  header.DLC = length;

  return (HAL_CAN_AddTxMessage(&hcan, &header, (uint8_t *)data, &mailbox) == HAL_OK) ? 1U : 0U;
}
