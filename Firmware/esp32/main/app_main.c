#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_protocol.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define UART_PORT UART_NUM_1
#define UART_TX_GPIO GPIO_NUM_17
#define UART_RX_GPIO GPIO_NUM_18
#define UART_BAUD_RATE 115200U
#define UART_RX_BUFFER_SIZE 512U
#define UART_TX_BUFFER_SIZE 512U

#define BRIDGE_ROLE_ESP32 2U
#define BRIDGE_HELLO_PERIOD_MS 1000U
#define BRIDGE_COMMAND_QUEUE_LENGTH 8U
#define BRIDGE_ACK_QUEUE_LENGTH 8U
#define BRIDGE_COMMAND_TIMEOUT_MS 500U
#define BRIDGE_COMMAND_MAX_RETRIES 2U
#define BRIDGE_CONSOLE_LINE_SIZE 96U

typedef struct
{
  uint16_t request_id;
  uint16_t command_id;
  uint32_t argument0;
  uint32_t argument1;
  uint32_t argument2;
} PendingCommand;

static const char *TAG = "industrial_bridge";

static BridgeProtocolParser s_parser;
static BridgeProtocolTelemetry s_telemetry;
static BridgeProtocolDiagnostics s_diagnostics;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_stm_online;
static bool s_diagnostics_valid;
static int64_t s_last_rx_us;
static QueueHandle_t s_command_queue;
static QueueHandle_t s_ack_queue;
static uint16_t s_command_sequence;
static uint16_t s_request_id;

static void SendFrame(const uint8_t *frame, uint16_t length)
{
  if ((frame != NULL) && (length > 0U))
  {
    (void)uart_write_bytes(UART_PORT, frame, length);
  }
}

static void SendHello(void)
{
  uint8_t frame[64];
  uint16_t length = BridgeProtocol_BuildHello(0U,
                                              BRIDGE_ROLE_ESP32,
                                              0x0100U,
                                              BRIDGE_CAP_WIFI,
                                              frame,
                                              sizeof(frame));
  SendFrame(frame, length);
}

static void HandleFrame(const BridgeProtocolFrame *frame)
{
  switch (frame->type)
  {
    case BRIDGE_MSG_HELLO:
    {
      BridgeProtocolHello hello;
      if (BridgeProtocol_ParseHello(frame, &hello))
      {
        ESP_LOGI(TAG, "STM32 hello: role=%u fw=%04X cap=%08lX",
                 hello.role,
                 hello.firmware_version,
                 (unsigned long)hello.capabilities);
      }
      break;
    }

    case BRIDGE_MSG_HEARTBEAT:
    {
      BridgeProtocolHeartbeat heartbeat;
      if (BridgeProtocol_ParseHeartbeat(frame, &heartbeat))
      {
        portENTER_CRITICAL(&s_state_lock);
        s_stm_online = true;
        s_last_rx_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGI(TAG, "heartbeat uptime=%lu health=%04X faults=%04X",
                 (unsigned long)heartbeat.uptime_ms,
                 heartbeat.health_flags,
                 heartbeat.fault_bits);
      }
      break;
    }

    case BRIDGE_MSG_TELEMETRY:
    {
      BridgeProtocolTelemetry telemetry;
      if (BridgeProtocol_ParseTelemetry(frame, &telemetry))
      {
        portENTER_CRITICAL(&s_state_lock);
        s_telemetry = telemetry;
        s_stm_online = true;
        s_last_rx_us = esp_timer_get_time();
        portEXIT_CRITICAL(&s_state_lock);
      }
      break;
    }

    case BRIDGE_MSG_DIAGNOSTICS:
    {
      BridgeProtocolDiagnostics diagnostics;
      if (BridgeProtocol_ParseDiagnostics(frame, &diagnostics))
      {
        portENTER_CRITICAL(&s_state_lock);
        s_diagnostics = diagnostics;
        s_diagnostics_valid = true;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGI(TAG,
                 "diag alive=%08lX uart_drop=%lu event_drop=%lu wdg=%lu",
                 (unsigned long)diagnostics.task_alive_bits,
                 (unsigned long)diagnostics.uart_rx_dropped,
                 (unsigned long)diagnostics.event_queue_dropped,
                 (unsigned long)diagnostics.watchdog_refresh_count);
      }
      break;
    }

    case BRIDGE_MSG_COMMAND_ACK:
    {
      BridgeProtocolAck ack;
      if (BridgeProtocol_ParseCommandAck(frame, &ack))
      {
        ESP_LOGI(TAG, "ack req=%u cmd=%04X result=%d detail=%u",
                 ack.request_id,
                 ack.command_id,
                 ack.result,
                 ack.detail);
        if (s_ack_queue != NULL)
        {
          (void)xQueueSend(s_ack_queue, &ack, 0U);
        }
      }
      break;
    }

    default:
      ESP_LOGD(TAG, "frame type=%02X length=%u", frame->type, frame->length);
      break;
  }
}

static void UartRxTask(void *argument)
{
  uint8_t buffer[128];
  BridgeProtocolFrame frame;

  (void)argument;

  BridgeProtocol_ParserInit(&s_parser);

  for (;;)
  {
    int received = uart_read_bytes(UART_PORT, buffer, sizeof(buffer), pdMS_TO_TICKS(20));

    if (received > 0)
    {
      for (int index = 0; index < received; index++)
      {
        if (BridgeProtocol_ParserPushByte(&s_parser, buffer[index], &frame))
        {
          HandleFrame(&frame);
        }
      }
    }
  }
}

static void HeartbeatTask(void *argument)
{
  uint8_t frame[64];
  uint16_t sequence = 0U;

  (void)argument;

  SendHello();

  for (;;)
  {
    bool online;
    int64_t last_rx_us;
    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    uint16_t length = BridgeProtocol_BuildHeartbeat(sequence++,
                                                    uptime_ms,
                                                    1U,
                                                    0U,
                                                    frame,
                                                    sizeof(frame));

    portENTER_CRITICAL(&s_state_lock);
    online = s_stm_online;
    last_rx_us = s_last_rx_us;
    s_stm_online = false;
    portEXIT_CRITICAL(&s_state_lock);

    if (!online && (last_rx_us > 0) &&
        ((esp_timer_get_time() - last_rx_us) > 3000000LL))
    {
      ESP_LOGW(TAG, "STM32 link timeout");
    }

    SendFrame(frame, length);
    vTaskDelay(pdMS_TO_TICKS(BRIDGE_HELLO_PERIOD_MS));
  }
}

static bool WaitForCommandAck(uint16_t request_id,
                              uint16_t command_id,
                              BridgeProtocolAck *ack_out)
{
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BRIDGE_COMMAND_TIMEOUT_MS);

  while (xTaskGetTickCount() < deadline)
  {
    TickType_t now = xTaskGetTickCount();
    TickType_t remaining = deadline - now;
    BridgeProtocolAck ack;

    if (xQueueReceive(s_ack_queue, &ack, remaining) != pdTRUE)
    {
      return false;
    }

    if ((ack.request_id == request_id) && (ack.command_id == command_id))
    {
      if (ack_out != NULL)
      {
        *ack_out = ack;
      }
      return true;
    }
  }

  return false;
}

static bool SubmitCommand(uint16_t command_id,
                          uint32_t argument0,
                          uint32_t argument1,
                          uint32_t argument2)
{
  PendingCommand command = {
      .command_id = command_id,
      .argument0 = argument0,
      .argument1 = argument1,
      .argument2 = argument2,
  };

  s_request_id++;
  if (s_request_id == 0U)
  {
    s_request_id = 1U;
  }
  command.request_id = s_request_id;

  if ((s_command_queue == NULL) ||
      (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE))
  {
    printf("command queue full\n");
    return false;
  }

  printf("queued req=%u cmd=%04X\n", command.request_id, command.command_id);
  return true;
}

static bool ParseUnsigned(const char *text, uint32_t maximum, uint32_t *value)
{
  char *end = NULL;
  unsigned long parsed;

  if ((text == NULL) || (value == NULL))
  {
    return false;
  }

  parsed = strtoul(text, &end, 0);
  if ((end == text) || (*end != '\0') || (parsed > maximum))
  {
    return false;
  }

  *value = (uint32_t)parsed;
  return true;
}

static void PrintStatus(void)
{
  BridgeProtocolTelemetry telemetry;
  BridgeProtocolDiagnostics diagnostics;
  bool online;
  bool diagnostics_valid;

  portENTER_CRITICAL(&s_state_lock);
  telemetry = s_telemetry;
  diagnostics = s_diagnostics;
  online = s_stm_online;
  diagnostics_valid = s_diagnostics_valid;
  portEXIT_CRITICAL(&s_state_lock);

  printf("STM32: %s\n", online ? "online" : "offline");
  printf("AI raw:");
  for (int index = 0; index < 8; index++)
  {
    printf(" %ld", (long)telemetry.ai_raw[index]);
  }
  printf("\nRTD: %ld mC\n", (long)telemetry.rtd_millicelsius);
  printf("DI=%02X RELAY=%02X 24V=%umV 5V=%umV FAULT=%04X\n",
         telemetry.di_bits,
         telemetry.relay_bits,
         telemetry.supply_mv[0],
         telemetry.supply_mv[1],
         telemetry.fault_bits);
  if (diagnostics_valid)
  {
    printf("ALIVE=%08lX UART_DROP=%lu EVENT_DROP=%lu WDG=%lu\n",
           (unsigned long)diagnostics.task_alive_bits,
           (unsigned long)diagnostics.uart_rx_dropped,
           (unsigned long)diagnostics.event_queue_dropped,
           (unsigned long)diagnostics.watchdog_refresh_count);
  }
}

static void ConsoleTask(void *argument)
{
  char line[BRIDGE_CONSOLE_LINE_SIZE];

  (void)argument;

  printf("\nindustrial bridge console\n");
  printf("type 'help' for commands\n");

  for (;;)
  {
    char *command;
    char *argument1;
    char *argument2;
    uint32_t value1;
    uint32_t value2;

    printf("bridge> ");
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    command = strtok(line, " \t\r\n");
    if (command == NULL)
    {
      continue;
    }

    argument1 = strtok(NULL, " \t\r\n");
    argument2 = strtok(NULL, " \t\r\n");

    if (strcmp(command, "help") == 0)
    {
      printf("relay <mask>        set relay output mask\n");
      printf("pulse <ch> <ms>     pulse one relay\n");
      printf("dac <ch> <value>    set DAC raw value\n");
      printf("clear <mask>        clear latched faults\n");
      printf("save | load         store or restore configuration\n");
      printf("status              print cached STM32 state\n");
    }
    else if (strcmp(command, "status") == 0)
    {
      PrintStatus();
    }
    else if ((strcmp(command, "relay") == 0) &&
             ParseUnsigned(argument1, 0xFFU, &value1))
    {
      (void)SubmitCommand(BRIDGE_CMD_SET_RELAY_MASK, value1, 0xFFU, 0U);
    }
    else if ((strcmp(command, "pulse") == 0) &&
             ParseUnsigned(argument1, 7U, &value1) &&
             ParseUnsigned(argument2, 60000U, &value2))
    {
      (void)SubmitCommand(BRIDGE_CMD_PULSE_RELAY, value1, value2, 0U);
    }
    else if ((strcmp(command, "dac") == 0) &&
             ParseUnsigned(argument1, 1U, &value1) &&
             ParseUnsigned(argument2, 4095U, &value2))
    {
      (void)SubmitCommand(BRIDGE_CMD_SET_ANALOG_OUTPUT, value1, value2, 0U);
    }
    else if ((strcmp(command, "clear") == 0) &&
             ParseUnsigned(argument1, 0xFFFFU, &value1))
    {
      (void)SubmitCommand(BRIDGE_CMD_CLEAR_FAULTS, value1, 0U, 0U);
    }
    else if (strcmp(command, "save") == 0)
    {
      (void)SubmitCommand(BRIDGE_CMD_SAVE_CONFIG, 0U, 0U, 0U);
    }
    else if (strcmp(command, "load") == 0)
    {
      (void)SubmitCommand(BRIDGE_CMD_LOAD_CONFIG, 0U, 0U, 0U);
    }
    else
    {
      printf("invalid command or arguments\n");
    }
  }
}

static void CommandRouterTask(void *argument)
{
  PendingCommand command;

  (void)argument;

  for (;;)
  {
    if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
    {
      continue;
    }

    bool acknowledged = false;
    BridgeProtocolAck ack = {0};

    for (uint8_t attempt = 0U; attempt <= BRIDGE_COMMAND_MAX_RETRIES; attempt++)
    {
      uint8_t frame[64];
      uint16_t length = BridgeProtocol_BuildCommand(s_command_sequence++,
                                                    command.request_id,
                                                    command.command_id,
                                                    command.argument0,
                                                    command.argument1,
                                                    command.argument2,
                                                    frame,
                                                    sizeof(frame));
      if (length == 0U)
      {
        printf("req=%u build failed\n", command.request_id);
        break;
      }

      SendFrame(frame, length);
      if (WaitForCommandAck(command.request_id, command.command_id, &ack))
      {
        acknowledged = true;
        break;
      }

      printf("req=%u timeout, retry %u/%u\n",
             command.request_id,
             (unsigned)(attempt + 1U),
             (unsigned)BRIDGE_COMMAND_MAX_RETRIES);
    }

    if (acknowledged)
    {
      printf("req=%u cmd=%04X result=%d detail=%u\n",
             ack.request_id,
             ack.command_id,
             ack.result,
             ack.detail);
    }
    else
    {
      printf("req=%u no ACK\n", command.request_id);
    }
  }
}

void app_main(void)
{
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);

  ESP_ERROR_CHECK(nvs_flash_init());

  uart_config_t config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));
  ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUFFER_SIZE, UART_TX_BUFFER_SIZE, 0, NULL, 0));

  s_command_queue = xQueueCreate(BRIDGE_COMMAND_QUEUE_LENGTH, sizeof(PendingCommand));
  s_ack_queue = xQueueCreate(BRIDGE_ACK_QUEUE_LENGTH, sizeof(BridgeProtocolAck));
  if ((s_command_queue == NULL) || (s_ack_queue == NULL))
  {
    ESP_LOGE(TAG, "failed to create command queues");
    return;
  }

  xTaskCreate(UartRxTask, "uart_rx", 4096, NULL, 8, NULL);
  xTaskCreate(HeartbeatTask, "heartbeat", 3072, NULL, 6, NULL);
  xTaskCreate(CommandRouterTask, "command_router", 3072, NULL, 6, NULL);
  xTaskCreate(ConsoleTask, "console", 4096, NULL, 4, NULL);
}
