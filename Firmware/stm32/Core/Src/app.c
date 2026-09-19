#include "app.h"

#include "adc.h"
#include "ads1256.h"
#include "analog_output.h"
#include "bridge_protocol.h"
#include "cmsis_os.h"
#include "config_store.h"
#include "device_state.h"
#include "digital_input.h"
#include "field_comm.h"
#include "main.h"
#include "max31865.h"
#include "relay_output.h"
#include "usart.h"

#define LED_PERIOD_MS 500U
#define HEARTBEAT_PERIOD_MS 1000U
#define TELEMETRY_PERIOD_MS 100U
#define SUPPLY_PERIOD_MS 50U
#define CONTROL_PERIOD_MS 5U
#define ACQ_PERIOD_MS 100U
#define RTD_PERIOD_MS 500U
#define UART_RX_BUFFER_SIZE 256U
#define UART_RX_QUEUE_LENGTH 512U
#define EVENT_QUEUE_LENGTH 16U

typedef struct
{
  uint32_t timestamp_ms;
  uint16_t event_code;
  uint32_t argument0;
  uint32_t argument1;
} AppEvent;

static volatile uint16_t s_supply_raw[2];
static uint8_t s_uart_rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint32_t s_uart_rx_event_count;
static osMessageQueueId_t s_uart_rx_queue;
static osMessageQueueId_t s_event_queue;
static osTimerId_t s_pulse_timers[8];
static uint16_t s_bridge_sequence;

static const osThreadAttr_t s_monitor_task_attributes = {
    .name = "monitorTask",
    .stack_size = 1024U,
    .priority = osPriorityAboveNormal,
};

static const osThreadAttr_t s_control_task_attributes = {
    .name = "controlTask",
    .stack_size = 1024U,
    .priority = osPriorityHigh,
};

static const osThreadAttr_t s_acq_task_attributes = {
    .name = "acqTask",
    .stack_size = 1536U,
    .priority = osPriorityHigh,
};

static const osThreadAttr_t s_rtd_task_attributes = {
    .name = "rtdTask",
    .stack_size = 1024U,
    .priority = osPriorityAboveNormal,
};

static const osThreadAttr_t s_bridge_task_attributes = {
    .name = "bridgeTask",
    .stack_size = 1536U,
    .priority = osPriorityNormal,
};

static void MonitorTask(void *argument);
static void ControlTask(void *argument);
static void AcqTask(void *argument);
static void RtdTask(void *argument);
static void BridgeTask(void *argument);
static void PulseTimerCallback(void *argument);
static void HandleCommandFrame(const BridgeProtocolFrame *frame);
static void SendAck(uint16_t request_id, uint16_t command_id, int16_t result, uint16_t detail);

void App_Init(void)
{
  DeviceState_Init();
  ConfigStore_Init();
  RelayOutput_Init();
  AnalogOutput_Init();
  DigitalInput_Init();
  MAX31865_Init();

  if (ADS1256_Init() == 0U)
  {
    DeviceState_SetFault(DEVICE_FAULT_ADS1256, 1U);
  }

  if (FieldComm_Init() == 0U)
  {
    DeviceState_SetFault(DEVICE_FAULT_COMM, 1U);
  }

  s_uart_rx_queue = osMessageQueueNew(UART_RX_QUEUE_LENGTH, sizeof(uint8_t), NULL);
  s_event_queue = osMessageQueueNew(EVENT_QUEUE_LENGTH, sizeof(AppEvent), NULL);
  if ((s_uart_rx_queue == NULL) || (s_event_queue == NULL))
  {
    Error_Handler();
  }

  for (uint8_t channel = 0U; channel < 8U; channel++)
  {
    s_pulse_timers[channel] = osTimerNew(PulseTimerCallback,
                                         osTimerOnce,
                                         (void *)(uintptr_t)channel,
                                         NULL);
    if (s_pulse_timers[channel] == NULL)
    {
      Error_Handler();
    }
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_supply_raw, 2U) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_uart_rx_buffer, UART_RX_BUFFER_SIZE) != HAL_OK)
  {
    Error_Handler();
  }
}

void App_CreateTasks(void)
{
  if (osThreadNew(MonitorTask, NULL, &s_monitor_task_attributes) == NULL)
  {
    Error_Handler();
  }

  if (osThreadNew(ControlTask, NULL, &s_control_task_attributes) == NULL)
  {
    Error_Handler();
  }

  if (osThreadNew(AcqTask, NULL, &s_acq_task_attributes) == NULL)
  {
    Error_Handler();
  }

  if (osThreadNew(RtdTask, NULL, &s_rtd_task_attributes) == NULL)
  {
    Error_Handler();
  }

  if (osThreadNew(BridgeTask, NULL, &s_bridge_task_attributes) == NULL)
  {
    Error_Handler();
  }
}

static void MonitorTask(void *argument)
{
  uint32_t last_led_tick = osKernelGetTickCount();

  (void)argument;

  for (;;)
  {
    uint32_t now = osKernelGetTickCount();
    uint16_t mv_24v = (uint16_t)(((uint32_t)s_supply_raw[0] * 36300U) / 4095U);
    uint16_t mv_5v = (uint16_t)(((uint32_t)s_supply_raw[1] * 6600U) / 4095U);

    DeviceState_UpdateSupplies(s_supply_raw[0], s_supply_raw[1], mv_24v, mv_5v);

    if ((now - last_led_tick) >= LED_PERIOD_MS)
    {
      last_led_tick = now;
      HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
    }

    osDelay(SUPPLY_PERIOD_MS);
  }
}

static void ControlTask(void *argument)
{
  uint8_t previous_di = 0U;

  (void)argument;

  for (;;)
  {
    uint8_t current_di;

    DigitalInput_Update();
    current_di = DigitalInput_GetStableBits();
    DeviceState_UpdateDigital(current_di, RelayOutput_GetMask());

    if (current_di != previous_di)
    {
      AppEvent event = {
          .timestamp_ms = osKernelGetTickCount(),
          .event_code = BRIDGE_EVENT_DIGITAL_INPUT,
          .argument0 = current_di,
          .argument1 = (uint32_t)(current_di ^ previous_di),
      };
      (void)osMessageQueuePut(s_event_queue, &event, 0U, 0U);
      previous_di = current_di;
    }

    osDelay(CONTROL_PERIOD_MS);
  }
}

static void AcqTask(void *argument)
{
  int32_t values[8];

  (void)argument;

  for (;;)
  {
    if (ADS1256_ReadAll(values) != 0U)
    {
      DeviceState_UpdateAnalogInputs(values);
    }
    else
    {
      DeviceState_SetFault(DEVICE_FAULT_ADS1256, 1U);
    }

    osDelay(ACQ_PERIOD_MS);
  }
}

static void RtdTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    int32_t temperature_millicelsius = 0;
    uint8_t valid = MAX31865_ReadMilliCelsius(&temperature_millicelsius);

    DeviceState_UpdateTemperature(temperature_millicelsius, (valid == 0U) ? 1U : 0U);
    osDelay(RTD_PERIOD_MS);
  }
}

static void BridgeTask(void *argument)
{
  BridgeProtocolParser parser;
  BridgeProtocolFrame frame;
  uint32_t last_heartbeat_tick = osKernelGetTickCount();
  uint32_t last_telemetry_tick = osKernelGetTickCount();

  (void)argument;

  BridgeProtocol_ParserInit(&parser);

  {
    uint8_t hello[32];
    uint16_t hello_length = BridgeProtocol_BuildHello(s_bridge_sequence++,
                                                      BRIDGE_ROLE_STM32,
                                                      0x0100U,
                                                      BRIDGE_CAP_ANALOG_INPUT |
                                                      BRIDGE_CAP_TEMPERATURE |
                                                      BRIDGE_CAP_DIGITAL_INPUT |
                                                      BRIDGE_CAP_RELAY_OUTPUT |
                                                      BRIDGE_CAP_ANALOG_OUTPUT |
                                                      BRIDGE_CAP_RS485 |
                                                      BRIDGE_CAP_RS232 |
                                                      BRIDGE_CAP_CAN |
                                                      BRIDGE_CAP_EEPROM,
                                                      hello,
                                                      sizeof(hello));
    if (hello_length > 0U)
    {
      (void)HAL_UART_Transmit(&huart1, hello, hello_length, 20U);
    }
  }

  for (;;)
  {
    uint8_t byte;
    uint32_t now = osKernelGetTickCount();
    DeviceStateSnapshot state = DeviceState_Get();
    uint8_t output[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
    uint16_t length;

    DeviceState_UpdateUart(s_uart_rx_event_count);

    while (osMessageQueueGet(s_uart_rx_queue, &byte, NULL, 0U) == osOK)
    {
      if (BridgeProtocol_ParserPushByte(&parser, byte, &frame))
      {
        if (frame.type == BRIDGE_MSG_COMMAND)
        {
          HandleCommandFrame(&frame);
        }
      }
    }

    {
      AppEvent event;
      while (osMessageQueueGet(s_event_queue, &event, NULL, 0U) == osOK)
      {
        length = BridgeProtocol_BuildEvent(s_bridge_sequence++,
                                           event.timestamp_ms,
                                           event.event_code,
                                           event.argument0,
                                           event.argument1,
                                           output,
                                           sizeof(output));
        if (length > 0U)
        {
          (void)HAL_UART_Transmit(&huart1, output, length, 20U);
        }
      }
    }

    if ((now - last_heartbeat_tick) >= HEARTBEAT_PERIOD_MS)
    {
      length = BridgeProtocol_BuildHeartbeat(s_bridge_sequence++,
                                             state.uptime_ms,
                                             1U,
                                             state.fault_bits,
                                             output,
                                             sizeof(output));
      last_heartbeat_tick = now;
      if (length > 0U)
      {
        (void)HAL_UART_Transmit(&huart1, output, length, 20U);
      }
    }

    if ((now - last_telemetry_tick) >= TELEMETRY_PERIOD_MS)
    {
      uint16_t supply_mv[2] = {state.supply_24v_mv, state.supply_5v_mv};

      length = BridgeProtocol_BuildTelemetry(s_bridge_sequence++,
                                             state.uptime_ms,
                                             state.ai_raw,
                                             state.rtd_millicelsius,
                                             state.di_bits,
                                             state.relay_bits,
                                             supply_mv,
                                             state.fault_bits,
                                             output,
                                             sizeof(output));
      last_telemetry_tick = now;
      if (length > 0U)
      {
        (void)HAL_UART_Transmit(&huart1, output, length, 20U);
      }
    }

    osDelay(10U);
  }
}

static void HandleCommandFrame(const BridgeProtocolFrame *frame)
{
  BridgeProtocolCommand command;
  int16_t result = BRIDGE_RESULT_UNSUPPORTED;
  uint16_t detail = 0U;

  if (BridgeProtocol_ParseCommand(frame, &command) == false)
  {
    SendAck(0U, 0U, BRIDGE_RESULT_INVALID_ARGUMENT, 0U);
    return;
  }

  switch (command.command_id)
  {
    case BRIDGE_CMD_SET_RELAY_MASK:
      if ((command.argument1 & 0xFFFFFF00U) == 0U)
      {
        RelayOutput_SetMask((uint8_t)command.argument0);
        {
          AppEvent event = {
              .timestamp_ms = osKernelGetTickCount(),
              .event_code = BRIDGE_EVENT_RELAY_CHANGE,
              .argument0 = RelayOutput_GetMask(),
              .argument1 = command.request_id,
          };
          (void)osMessageQueuePut(s_event_queue, &event, 0U, 0U);
        }
        result = BRIDGE_RESULT_OK;
      }
      else
      {
        result = BRIDGE_RESULT_INVALID_ARGUMENT;
      }
      break;

    case BRIDGE_CMD_SET_ANALOG_OUTPUT:
      if ((command.argument0 <= 1U) && (command.argument1 <= 4095U))
      {
        result = (AnalogOutput_SetRaw((uint8_t)command.argument0,
                                      (uint16_t)command.argument1) != 0U)
                     ? BRIDGE_RESULT_OK
                     : BRIDGE_RESULT_NOT_READY;
      }
      else
      {
        result = BRIDGE_RESULT_OUT_OF_RANGE;
      }
      break;

    case BRIDGE_CMD_PULSE_RELAY:
      if ((command.argument0 < 8U) &&
          (command.argument1 > 0U) &&
          (command.argument1 <= 60000U))
      {
        uint8_t mask = RelayOutput_GetMask();
        mask |= (uint8_t)(1U << command.argument0);
        RelayOutput_SetMask(mask);
        {
          AppEvent event = {
              .timestamp_ms = osKernelGetTickCount(),
              .event_code = BRIDGE_EVENT_RELAY_CHANGE,
              .argument0 = mask,
              .argument1 = command.request_id,
          };
          (void)osMessageQueuePut(s_event_queue, &event, 0U, 0U);
        }
        result = (osTimerStart(s_pulse_timers[command.argument0],
                               command.argument1) == osOK)
                     ? BRIDGE_RESULT_OK
                     : BRIDGE_RESULT_BUSY;
      }
      else
      {
        result = BRIDGE_RESULT_INVALID_ARGUMENT;
      }
      break;

    case BRIDGE_CMD_CLEAR_FAULTS:
      DeviceState_ClearFaults((uint16_t)command.argument0);
      result = BRIDGE_RESULT_OK;
      break;

    case BRIDGE_CMD_SAVE_CONFIG:
      if (ConfigStore_Save() != 0U)
      {
        DeviceState_ClearFaults(DEVICE_FAULT_CONFIG);
        result = BRIDGE_RESULT_OK;
      }
      else
      {
        DeviceState_SetFault(DEVICE_FAULT_CONFIG, 1U);
        result = BRIDGE_RESULT_STORAGE_ERROR;
      }
      break;

    case BRIDGE_CMD_LOAD_CONFIG:
      if (ConfigStore_Load() != 0U)
      {
        DeviceState_ClearFaults(DEVICE_FAULT_CONFIG);
        result = BRIDGE_RESULT_OK;
      }
      else
      {
        DeviceState_SetFault(DEVICE_FAULT_CONFIG, 1U);
        result = BRIDGE_RESULT_STORAGE_ERROR;
      }
      break;

    default:
      result = BRIDGE_RESULT_UNSUPPORTED;
      break;
  }

  SendAck(command.request_id, command.command_id, result, detail);
}

static void PulseTimerCallback(void *argument)
{
  uint8_t channel = (uint8_t)(uintptr_t)argument;
  uint8_t mask = RelayOutput_GetMask() & (uint8_t)~(1U << channel);
  RelayOutput_SetMask(mask);
}

static void SendAck(uint16_t request_id, uint16_t command_id, int16_t result, uint16_t detail)
{
  uint8_t output[32];
  uint16_t length = BridgeProtocol_BuildCommandAck(s_bridge_sequence++,
                                                   request_id,
                                                   command_id,
                                                   result,
                                                   detail,
                                                   output,
                                                   sizeof(output));

  if (length > 0U)
  {
    (void)HAL_UART_Transmit(&huart1, output, length, 20U);
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart->Instance == USART1)
  {
    uint16_t index;

    for (index = 0U; index < size; index++)
    {
      (void)osMessageQueuePut(s_uart_rx_queue, &s_uart_rx_buffer[index], 0U, 0U);
    }

    s_uart_rx_event_count++;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_uart_rx_buffer, UART_RX_BUFFER_SIZE);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_uart_rx_buffer, UART_RX_BUFFER_SIZE);
  }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    Error_Handler();
  }
}
