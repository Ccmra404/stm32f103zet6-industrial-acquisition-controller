#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_protocol.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs.h"
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

#define NETWORK_NVS_NAMESPACE "netcfg"
#define NETWORK_NVS_SSID "ssid"
#define NETWORK_NVS_PASSWORD "password"
#define NETWORK_NVS_BROKER "broker"
#define NETWORK_SSID_SIZE 33U
#define NETWORK_PASSWORD_SIZE 65U
#define NETWORK_BROKER_SIZE 128U
#define NETWORK_TELEMETRY_TOPIC "industrial/telemetry"
#define NETWORK_PUBLISH_PERIOD_MS 2000U

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

static esp_mqtt_client_handle_t s_mqtt_client;
static char s_wifi_ssid[NETWORK_SSID_SIZE];
static char s_wifi_password[NETWORK_PASSWORD_SIZE];
static char s_mqtt_broker[NETWORK_BROKER_SIZE];
static volatile bool s_wifi_connected;
static volatile bool s_mqtt_connected;
static volatile bool s_network_reload;

static void NetworkWifiEventHandler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data);
static void NetworkMqttEventHandler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data);

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
  printf("WiFi: %s SSID=%s\n",
         s_wifi_connected ? "connected" : "disconnected",
         (s_wifi_ssid[0] != '\0') ? s_wifi_ssid : "(not configured)");
  printf("MQTT: %s broker=%s\n",
         s_mqtt_connected ? "connected" : "disconnected",
         (s_mqtt_broker[0] != '\0') ? s_mqtt_broker : "(not configured)");
}

static void NetworkLoadConfig(void)
{
  nvs_handle_t handle;
  size_t length;

  memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
  memset(s_wifi_password, 0, sizeof(s_wifi_password));
  memset(s_mqtt_broker, 0, sizeof(s_mqtt_broker));

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
  {
    return;
  }

  length = sizeof(s_wifi_ssid);
  (void)nvs_get_str(handle, NETWORK_NVS_SSID, s_wifi_ssid, &length);
  length = sizeof(s_wifi_password);
  (void)nvs_get_str(handle, NETWORK_NVS_PASSWORD, s_wifi_password, &length);
  length = sizeof(s_mqtt_broker);
  (void)nvs_get_str(handle, NETWORK_NVS_BROKER, s_mqtt_broker, &length);
  nvs_close(handle);
}

static void NetworkSaveWifi(const char *ssid, const char *password)
{
  nvs_handle_t handle;

  if ((ssid == NULL) || (password == NULL))
  {
    return;
  }

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_str(handle, NETWORK_NVS_SSID, ssid);
  (void)nvs_set_str(handle, NETWORK_NVS_PASSWORD, password);
  (void)nvs_commit(handle);
  nvs_close(handle);
  s_network_reload = true;
  printf("WiFi config saved, reconnecting\n");
}

static void NetworkSaveBroker(const char *uri)
{
  nvs_handle_t handle;

  if (uri == NULL)
  {
    return;
  }

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_str(handle, NETWORK_NVS_BROKER, uri);
  (void)nvs_commit(handle);
  nvs_close(handle);
  s_network_reload = true;
  printf("MQTT broker saved\n");
}

static void NetworkStartMqtt(void)
{
  esp_mqtt_client_config_t mqtt_config = {
      .broker.address.uri = s_mqtt_broker,
  };

  if ((s_mqtt_broker[0] == '\0') || (s_mqtt_client != NULL))
  {
    return;
  }

  s_mqtt_client = esp_mqtt_client_init(&mqtt_config);
  if (s_mqtt_client == NULL)
  {
    ESP_LOGE(TAG, "MQTT client init failed");
    return;
  }

  ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client,
                                                 ESP_EVENT_ANY_ID,
                                                 NetworkMqttEventHandler,
                                                 NULL));
  ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));
}

static void NetworkStopMqtt(void)
{
  if (s_mqtt_client == NULL)
  {
    return;
  }

  (void)esp_mqtt_client_stop(s_mqtt_client);
  (void)esp_mqtt_client_destroy(s_mqtt_client);
  s_mqtt_client = NULL;
  s_mqtt_connected = false;
}

static void NetworkApplyConfig(void)
{
  NetworkLoadConfig();
  NetworkStopMqtt();

  if (s_wifi_ssid[0] != '\0')
  {
    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, s_wifi_ssid, sizeof(wifi_config.sta.ssid) - 1U);
    strncpy((char *)wifi_config.sta.password,
            s_wifi_password,
            sizeof(wifi_config.sta.password) - 1U);
    wifi_config.sta.threshold.authmode =
        (s_wifi_password[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    (void)esp_wifi_disconnect();
    (void)esp_wifi_connect();
  }
  else
  {
    (void)esp_wifi_disconnect();
  }

  s_network_reload = false;
}

static void NetworkWifiEventHandler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
  (void)argument;
  (void)event_data;

  if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START))
  {
    if (s_wifi_ssid[0] != '\0')
    {
      (void)esp_wifi_connect();
    }
  }
  else if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED))
  {
    s_wifi_connected = false;
    s_mqtt_connected = false;
    if (s_wifi_ssid[0] != '\0')
    {
      (void)esp_wifi_connect();
    }
  }
  else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP))
  {
    s_wifi_connected = true;
    ESP_LOGI(TAG, "WiFi connected");
  }
}

static void NetworkMqttEventHandler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
  (void)argument;
  (void)event_base;
  (void)event_data;

  if (event_id == MQTT_EVENT_CONNECTED)
  {
    s_mqtt_connected = true;
    ESP_LOGI(TAG, "MQTT connected");
  }
  else if (event_id == MQTT_EVENT_DISCONNECTED)
  {
    s_mqtt_connected = false;
    ESP_LOGW(TAG, "MQTT disconnected");
  }
  else if (event_id == MQTT_EVENT_ERROR)
  {
    ESP_LOGE(TAG, "MQTT error");
  }
}

static void NetworkPublishTelemetry(void)
{
  BridgeProtocolTelemetry telemetry;
  BridgeProtocolDiagnostics diagnostics;
  bool diagnostics_valid;
  char payload[512];
  int position = 0;

  if ((s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    return;
  }

  portENTER_CRITICAL(&s_state_lock);
  telemetry = s_telemetry;
  diagnostics = s_diagnostics;
  diagnostics_valid = s_diagnostics_valid;
  portEXIT_CRITICAL(&s_state_lock);

  position += snprintf(&payload[position],
                       sizeof(payload) - (size_t)position,
                       "{\"ai_raw\":[");
  for (uint8_t index = 0U; index < 8U; index++)
  {
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         "%s%ld",
                         (index == 0U) ? "" : ",",
                         (long)telemetry.ai_raw[index]);
  }
  position += snprintf(&payload[position],
                       sizeof(payload) - (size_t)position,
                       "],\"rtd_mc\":%ld,\"di\":%u,\"relay\":%u,"
                       "\"supply_mv\":[%u,%u],\"fault\":%u",
                       (long)telemetry.rtd_millicelsius,
                       telemetry.di_bits,
                       telemetry.relay_bits,
                       telemetry.supply_mv[0],
                       telemetry.supply_mv[1],
                       telemetry.fault_bits);
  if (diagnostics_valid)
  {
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         ",\"alive\":%lu,\"wdg\":%lu",
                         (unsigned long)diagnostics.task_alive_bits,
                         (unsigned long)diagnostics.watchdog_refresh_count);
  }
  (void)snprintf(&payload[position],
                 sizeof(payload) - (size_t)position,
                 "}");
  (void)esp_mqtt_client_publish(s_mqtt_client,
                                NETWORK_TELEMETRY_TOPIC,
                                payload,
                                0,
                                0,
                                0);
}

static void NetworkTask(void *argument)
{
  TickType_t last_publish = 0U;

  (void)argument;

  for (;;)
  {
    if (s_network_reload)
    {
      NetworkApplyConfig();
    }

    if (s_wifi_connected && (s_mqtt_client == NULL))
    {
      NetworkStartMqtt();
    }

    if ((xTaskGetTickCount() - last_publish) >= pdMS_TO_TICKS(NETWORK_PUBLISH_PERIOD_MS))
    {
      NetworkPublishTelemetry();
      last_publish = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(1000U));
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
      printf("wifi <ssid> <pass>  save WiFi credentials and reconnect\n");
      printf("mqtt <uri>          save MQTT broker URI\n");
      printf("reconnect           apply saved network configuration\n");
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
    else if ((strcmp(command, "wifi") == 0) &&
             (argument1 != NULL) &&
             (argument2 != NULL))
    {
      NetworkSaveWifi(argument1, (strcmp(argument2, "-") == 0) ? "" : argument2);
    }
    else if ((strcmp(command, "mqtt") == 0) && (argument1 != NULL))
    {
      NetworkSaveBroker(argument1);
    }
    else if (strcmp(command, "reconnect") == 0)
    {
      s_network_reload = true;
      printf("network reconnect requested\n");
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
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  (void)esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      NetworkWifiEventHandler,
                                                      NULL,
                                                      NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      NetworkWifiEventHandler,
                                                      NULL,
                                                      NULL));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  NetworkLoadConfig();
  s_network_reload = true;

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
  xTaskCreate(NetworkTask, "network", 4096, NULL, 5, NULL);
  xTaskCreate(ConsoleTask, "console", 4096, NULL, 4, NULL);
}
