#include "field_comm.h"

#include "can.h"
#include "main.h"
#include "usart.h"

static uint8_t s_rs232_byte;
static uint8_t s_rs232_rx_buffer[FIELD_COMM_RS232_RX_BUFFER_SIZE];
static volatile uint16_t s_rs232_rx_head;
static volatile uint16_t s_rs232_rx_tail;
static volatile uint32_t s_rs232_rx_count;
static volatile uint32_t s_rs232_rx_dropped;

static FieldCommCanFrame s_can_rx_queue[FIELD_COMM_CAN_RX_DEPTH];
static volatile uint8_t s_can_rx_head;
static volatile uint8_t s_can_rx_tail;
static volatile uint32_t s_can_rx_count;
static volatile uint32_t s_can_rx_dropped;

uint8_t FieldComm_Init(void)
{
  CAN_FilterTypeDef filter = {0};
  uint32_t mailboxes;

  s_rs232_rx_head = 0U;
  s_rs232_rx_tail = 0U;
  s_rs232_rx_count = 0U;
  s_rs232_rx_dropped = 0U;
  s_can_rx_head = 0U;
  s_can_rx_tail = 0U;
  s_can_rx_count = 0U;
  s_can_rx_dropped = 0U;

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

  if (HAL_UART_Receive_IT(&huart4, &s_rs232_byte, 1U) != HAL_OK)
  {
    return 0U;
  }

  (void)mailboxes;
  return 1U;
}

uint8_t FieldComm_ReadRs485Byte(uint8_t *byte, uint32_t timeout)
{
  if (byte == 0)
  {
    return 0U;
  }

  return (HAL_UART_Receive(&huart2, byte, 1U, timeout) == HAL_OK) ? 1U : 0U;
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

uint16_t FieldComm_Rs232Available(void)
{
  return (uint16_t)((s_rs232_rx_head - s_rs232_rx_tail) &
                    (FIELD_COMM_RS232_RX_BUFFER_SIZE - 1U));
}

uint8_t FieldComm_ReadRs232Byte(uint8_t *byte, uint32_t timeout)
{
  uint32_t start;

  if (byte == 0)
  {
    return 0U;
  }

  start = HAL_GetTick();
  do
  {
    if (s_rs232_rx_tail != s_rs232_rx_head)
    {
      *byte = s_rs232_rx_buffer[s_rs232_rx_tail];
      s_rs232_rx_tail = (uint16_t)((s_rs232_rx_tail + 1U) &
                                   (FIELD_COMM_RS232_RX_BUFFER_SIZE - 1U));
      return 1U;
    }
  } while ((HAL_GetTick() - start) < timeout);

  return 0U;
}

uint16_t FieldComm_ReadRs232Bytes(uint8_t *buffer, uint16_t max_length)
{
  uint16_t count = 0U;

  if ((buffer == 0) || (max_length == 0U))
  {
    return 0U;
  }

  while ((count < max_length) && (s_rs232_rx_tail != s_rs232_rx_head))
  {
    buffer[count] = s_rs232_rx_buffer[s_rs232_rx_tail];
    s_rs232_rx_tail = (uint16_t)((s_rs232_rx_tail + 1U) &
                                 (FIELD_COMM_RS232_RX_BUFFER_SIZE - 1U));
    count++;
  }

  return count;
}

uint16_t FieldComm_CanAvailable(void)
{
  return (uint16_t)((s_can_rx_head - s_can_rx_tail) & (FIELD_COMM_CAN_RX_DEPTH - 1U));
}

uint8_t FieldComm_ReadCanFrame(FieldCommCanFrame *frame, uint32_t timeout)
{
  uint32_t start;

  if (frame == 0)
  {
    return 0U;
  }

  start = HAL_GetTick();
  do
  {
    if (s_can_rx_tail != s_can_rx_head)
    {
      *frame = s_can_rx_queue[s_can_rx_tail];
      s_can_rx_tail = (uint8_t)((s_can_rx_tail + 1U) & (FIELD_COMM_CAN_RX_DEPTH - 1U));
      return 1U;
    }
  } while ((HAL_GetTick() - start) < timeout);

  return 0U;
}

uint32_t FieldComm_Rs232RxCount(void)
{
  return s_rs232_rx_count;
}

uint32_t FieldComm_CanRxCount(void)
{
  return s_can_rx_count;
}

uint32_t FieldComm_Rs232Dropped(void)
{
  return s_rs232_rx_dropped;
}

uint32_t FieldComm_CanDropped(void)
{
  return s_can_rx_dropped;
}

void FieldComm_OnUartError(UART_HandleTypeDef *huart)
{
  if ((huart != 0) && (huart->Instance == UART4))
  {
    (void)HAL_UART_Receive_IT(&huart4, &s_rs232_byte, 1U);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((huart != 0) && (huart->Instance == UART4))
  {
    uint16_t next = (uint16_t)((s_rs232_rx_head + 1U) &
                               (FIELD_COMM_RS232_RX_BUFFER_SIZE - 1U));

    if (next == s_rs232_rx_tail)
    {
      s_rs232_rx_dropped++;
    }
    else
    {
      s_rs232_rx_buffer[s_rs232_rx_head] = s_rs232_byte;
      s_rs232_rx_head = next;
      s_rs232_rx_count++;
    }

    (void)HAL_UART_Receive_IT(&huart4, &s_rs232_byte, 1U);
  }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can_handle)
{
  CAN_RxHeaderTypeDef header = {0};
  uint8_t data[FIELD_COMM_CAN_MAX_DATA] = {0};
  uint8_t next;

  if ((can_handle == 0) || (can_handle->Instance != CAN1))
  {
    return;
  }

  if (HAL_CAN_GetRxMessage(can_handle, CAN_RX_FIFO0, &header, data) != HAL_OK)
  {
    return;
  }

  next = (uint8_t)((s_can_rx_head + 1U) & (FIELD_COMM_CAN_RX_DEPTH - 1U));
  if (next == s_can_rx_tail)
  {
    s_can_rx_dropped++;
    return;
  }

  s_can_rx_queue[s_can_rx_head].identifier =
      (header.IDE == CAN_ID_EXT) ? header.ExtId : header.StdId;
  s_can_rx_queue[s_can_rx_head].length =
      (header.DLC > FIELD_COMM_CAN_MAX_DATA) ? FIELD_COMM_CAN_MAX_DATA : (uint8_t)header.DLC;
  for (uint8_t index = 0U; index < s_can_rx_queue[s_can_rx_head].length; index++)
  {
    s_can_rx_queue[s_can_rx_head].data[index] = data[index];
  }
  s_can_rx_head = next;
  s_can_rx_count++;
}
