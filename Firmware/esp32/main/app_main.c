#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_protocol.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link_health.h"
#include "mqtt_client.h"
#include "network_security.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "web_console.h"

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
#define NETWORK_NVS_MQTT_USER "mqtt_user"
#define NETWORK_NVS_MQTT_PASSWORD "mqtt_pass"
#define NETWORK_NVS_COMMAND_TOKEN "cmd_token"
#define NETWORK_NVS_WEB_USER "web_user"
#define NETWORK_NVS_WEB_PASSWORD "web_pass"
#define NETWORK_NVS_PLATFORM "platform"
#define NETWORK_SSID_SIZE 33U
#define NETWORK_PASSWORD_SIZE 65U
#define NETWORK_BROKER_SIZE 128U
#define NETWORK_MQTT_USER_SIZE 65U
#define NETWORK_MQTT_PASSWORD_SIZE 65U
#define NETWORK_COMMAND_TOKEN_SIZE 65U
#define NETWORK_WEB_USER_SIZE 65U
#define NETWORK_WEB_PASSWORD_SIZE 65U
#define NETWORK_TELEMETRY_TOPIC "industrial/telemetry"
#define NETWORK_EVENT_TOPIC "industrial/event"
#define NETWORK_COMMAND_TOPIC "industrial/command"
#define NETWORK_COMMAND_ACK_TOPIC "industrial/ack"
#define NETWORK_AVAILABILITY_TOPIC "industrial/availability"
#define NETWORK_BUS_TOPIC "industrial/bus"
#define NETWORK_HA_DISCOVERY_PREFIX "homeassistant"
#define NETWORK_HA_DEVICE_ID "industrial_controller_esp32"
#define NETWORK_HA_TEMP_UNIT "\xC2\xB0" "C"
#define NETWORK_THINGSBOARD_TELEMETRY_TOPIC "v1/devices/me/telemetry"
#define NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX "v1/devices/me/rpc/request/"
#define NETWORK_THINGSBOARD_RPC_RESPONSE_PREFIX "v1/devices/me/rpc/response/"
#define NETWORK_THINGSBOARD_BROKER "mqtts://mqtt.thingsboard.cloud:8883"
#define NETWORK_PUBLISH_PERIOD_MS 2000U

typedef enum
{
  NETWORK_PLATFORM_CUSTOM = 0,
  NETWORK_PLATFORM_THINGSBOARD,
} NetworkPlatform;

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
static BridgeProtocolBusRx s_last_bus_rx;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_diagnostics_valid;
static bool s_last_bus_rx_valid;
static uint32_t s_rs232_rx_frames;
static uint32_t s_can_rx_frames;
static uint32_t s_bus_rx_bytes;
static QueueHandle_t s_command_queue;
static QueueHandle_t s_ack_queue;
static uint16_t s_command_sequence;
static uint16_t s_request_id;
static uint32_t s_ack_sequence;

static esp_mqtt_client_handle_t s_mqtt_client;
static char s_wifi_ssid[NETWORK_SSID_SIZE];
static char s_wifi_password[NETWORK_PASSWORD_SIZE];
static char s_mqtt_broker[NETWORK_BROKER_SIZE];
static char s_mqtt_user[NETWORK_MQTT_USER_SIZE];
static char s_mqtt_password[NETWORK_MQTT_PASSWORD_SIZE];
static char s_command_token[NETWORK_COMMAND_TOKEN_SIZE];
static char s_web_user[NETWORK_WEB_USER_SIZE];
static char s_web_password[NETWORK_WEB_PASSWORD_SIZE];
static NetworkPlatform s_network_platform;
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

static void NetworkPublishLinkEvent(const LinkHealthSnapshot *link)
{
  char payload[192];
  int length;

  if ((s_network_platform == NETWORK_PLATFORM_THINGSBOARD) ||
      (link == NULL) || (s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    return;
  }

  length = snprintf(payload,
                    sizeof(payload),
                    "{\"event\":\"link_state\",\"state\":\"%s\","
                    "\"age_ms\":%lu,\"reconnects\":%lu,\"timeouts\":%lu}",
                    LinkHealth_StateName(link->state),
                    (unsigned long)link->last_rx_age_ms,
                    (unsigned long)link->reconnect_count,
                    (unsigned long)link->timeout_count);
  if (length <= 0)
  {
    return;
  }

  (void)esp_mqtt_client_publish(s_mqtt_client,
                                NETWORK_EVENT_TOPIC,
                                payload,
                                0,
                                1,
                                1);
}

static cJSON *NetworkCreateDiscoveryRoot(const char *name,
                                         const char *object_id,
                                         bool with_availability)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *device;
  cJSON *identifiers;

  if (root == NULL)
  {
    return NULL;
  }

  cJSON_AddStringToObject(root, "name", name);
  cJSON_AddStringToObject(root, "object_id", object_id);
  cJSON_AddStringToObject(root, "unique_id", object_id);
  if (with_availability)
  {
    cJSON_AddStringToObject(root,
                            "availability_topic",
                            NETWORK_AVAILABILITY_TOPIC);
    cJSON_AddStringToObject(root, "payload_available", "online");
    cJSON_AddStringToObject(root, "payload_not_available", "offline");
  }

  device = cJSON_CreateObject();
  identifiers = cJSON_CreateArray();
  if ((device == NULL) || (identifiers == NULL))
  {
    cJSON_Delete(device);
    cJSON_Delete(identifiers);
    cJSON_Delete(root);
    return NULL;
  }

  cJSON_AddItemToArray(identifiers, cJSON_CreateString(NETWORK_HA_DEVICE_ID));
  cJSON_AddItemToObject(device, "identifiers", identifiers);
  cJSON_AddStringToObject(device, "name", "工业采集控制终端");
  cJSON_AddStringToObject(device, "manufacturer", "STM32 + ESP32");
  cJSON_AddStringToObject(device, "model", "Industrial Acquisition Controller");
  cJSON_AddStringToObject(device, "sw_version", "1.1.0");
  cJSON_AddItemToObject(root, "device", device);
  return root;
}

static void NetworkPublishDiscoveryPayload(const char *component,
                                           const char *object_id,
                                           cJSON *root)
{
  char topic[192];
  char default_entity_id[160];
  char *payload;

  if ((root == NULL) || (s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    cJSON_Delete(root);
    return;
  }

  (void)snprintf(topic,
                 sizeof(topic),
                 "%s/%s/%s/%s/config",
                 NETWORK_HA_DISCOVERY_PREFIX,
                 component,
                 NETWORK_HA_DEVICE_ID,
                 object_id);
  (void)snprintf(default_entity_id,
                 sizeof(default_entity_id),
                 "%s.%s",
                 component,
                 object_id);
  cJSON_AddStringToObject(root, "default_entity_id", default_entity_id);
  payload = cJSON_PrintUnformatted(root);
  if (payload != NULL)
  {
    (void)esp_mqtt_client_publish(s_mqtt_client,
                                  topic,
                                  payload,
                                  0,
                                  1,
                                  1);
    cJSON_free(payload);
  }
  cJSON_Delete(root);
}

static void NetworkPublishDiscoverySensor(const char *object_id,
                                          const char *name,
                                          const char *state_topic,
                                          const char *value_template,
                                          const char *unit,
                                          const char *device_class,
                                          const char *state_class,
                                          const char *icon,
                                          const char *attributes_topic,
                                          bool diagnostic,
                                          bool expire)
{
  cJSON *root = NetworkCreateDiscoveryRoot(name, object_id, true);

  if (root == NULL)
  {
    return;
  }

  cJSON_AddStringToObject(root, "state_topic", state_topic);
  if (value_template != NULL)
  {
    cJSON_AddStringToObject(root, "value_template", value_template);
  }
  if (unit != NULL)
  {
    cJSON_AddStringToObject(root, "unit_of_measurement", unit);
  }
  if (device_class != NULL)
  {
    cJSON_AddStringToObject(root, "device_class", device_class);
  }
  if (state_class != NULL)
  {
    cJSON_AddStringToObject(root, "state_class", state_class);
  }
  if (icon != NULL)
  {
    cJSON_AddStringToObject(root, "icon", icon);
  }
  if (attributes_topic != NULL)
  {
    cJSON_AddStringToObject(root, "json_attributes_topic", attributes_topic);
  }
  if (diagnostic)
  {
    cJSON_AddStringToObject(root, "entity_category", "diagnostic");
  }
  if (expire)
  {
    cJSON_AddNumberToObject(root, "expire_after", 10.0);
  }
  NetworkPublishDiscoveryPayload("sensor", object_id, root);
}

static void NetworkPublishDiscoveryBinarySensor(const char *object_id,
                                                const char *name,
                                                const char *state_topic,
                                                const char *value_template,
                                                const char *payload_on,
                                                const char *payload_off,
                                                const char *device_class,
                                                const char *icon,
                                                bool with_availability,
                                                bool diagnostic)
{
  cJSON *root = NetworkCreateDiscoveryRoot(name,
                                           object_id,
                                           with_availability);

  if (root == NULL)
  {
    return;
  }

  cJSON_AddStringToObject(root, "state_topic", state_topic);
  if (value_template != NULL)
  {
    cJSON_AddStringToObject(root, "value_template", value_template);
  }
  if (payload_on != NULL)
  {
    cJSON_AddStringToObject(root, "payload_on", payload_on);
  }
  if (payload_off != NULL)
  {
    cJSON_AddStringToObject(root, "payload_off", payload_off);
  }
  if (device_class != NULL)
  {
    cJSON_AddStringToObject(root, "device_class", device_class);
  }
  if (icon != NULL)
  {
    cJSON_AddStringToObject(root, "icon", icon);
  }
  if (diagnostic)
  {
    cJSON_AddStringToObject(root, "entity_category", "diagnostic");
  }
  NetworkPublishDiscoveryPayload("binary_sensor", object_id, root);
}

static void NetworkPublishDiscoveryButton(const char *object_id,
                                          const char *name,
                                          const char *icon,
                                          const char *payload_press)
{
  cJSON *root = NetworkCreateDiscoveryRoot(name, object_id, true);

  if (root == NULL)
  {
    return;
  }

  cJSON_AddStringToObject(root, "command_topic", NETWORK_COMMAND_TOPIC);
  cJSON_AddStringToObject(root, "payload_press", payload_press);
  cJSON_AddStringToObject(root, "icon", icon);
  cJSON_AddNumberToObject(root, "qos", 1.0);
  NetworkPublishDiscoveryPayload("button", object_id, root);
}

static void NetworkPublishDiscoveryNumber(const char *object_id,
                                          const char *name,
                                          const char *icon,
                                          double minimum,
                                          double maximum,
                                          double step,
                                          const char *command_template,
                                          const char *value_template)
{
  cJSON *root = NetworkCreateDiscoveryRoot(name, object_id, true);

  if (root == NULL)
  {
    return;
  }

  cJSON_AddStringToObject(root, "command_topic", NETWORK_COMMAND_TOPIC);
  cJSON_AddStringToObject(root, "state_topic", NETWORK_TELEMETRY_TOPIC);
  cJSON_AddStringToObject(root, "command_template", command_template);
  cJSON_AddStringToObject(root, "value_template", value_template);
  cJSON_AddStringToObject(root, "mode", "box");
  cJSON_AddStringToObject(root, "icon", icon);
  cJSON_AddNumberToObject(root, "min", minimum);
  cJSON_AddNumberToObject(root, "max", maximum);
  cJSON_AddNumberToObject(root, "step", step);
  cJSON_AddNumberToObject(root, "qos", 1.0);
  NetworkPublishDiscoveryPayload("number", object_id, root);
}

static void NetworkPublishHomeAssistantDiscovery(void)
{
  char object_id[64];
  char name[48];
  char value_template[96];
  const char relay_command_template[] =
      "{\"id\":{{ (now().timestamp() * 1000) | int % 65535 }},"
      "\"command\":\"relay\",\"mask\":{{ value }}}";

  if ((s_network_platform != NETWORK_PLATFORM_CUSTOM) ||
      (s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    return;
  }

  (void)esp_mqtt_client_publish(s_mqtt_client,
                                NETWORK_AVAILABILITY_TOPIC,
                                "online",
                                0,
                                1,
                                1);

  NetworkPublishDiscoverySensor("industrial_controller_link_state",
                                "STM32 链路状态",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.link_state }}",
                                NULL,
                                NULL,
                                NULL,
                                "mdi:lan-connect",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_link_age",
                                "链路延迟",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.link_age_ms }}",
                                "ms",
                                NULL,
                                "measurement",
                                "mdi:timer-outline",
                                NULL,
                                true,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_reconnects",
                                "重连次数",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.reconnects }}",
                                NULL,
                                NULL,
                                "total_increasing",
                                "mdi:restart",
                                NULL,
                                true,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_timeouts",
                                "超时次数",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.timeouts }}",
                                NULL,
                                NULL,
                                "total_increasing",
                                "mdi:timer-alert-outline",
                                NULL,
                                true,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_rtd_temperature",
                                "RTD 温度",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ (value_json.rtd_mc | float(0) / 1000) | round(2) }}",
                                NETWORK_HA_TEMP_UNIT,
                                "temperature",
                                "measurement",
                                "mdi:thermometer",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_supply_24v",
                                "24V 电源",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ (value_json.supply_mv[0] | float(0) / 1000) | round(2) }}",
                                "V",
                                "voltage",
                                "measurement",
                                "mdi:power-plug",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_supply_5v",
                                "5V 电源",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ (value_json.supply_mv[1] | float(0) / 1000) | round(2) }}",
                                "V",
                                "voltage",
                                "measurement",
                                "mdi:usb-flash-drive",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_di_bitmap",
                                "DI 位图",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.di }}",
                                NULL,
                                NULL,
                                "measurement",
                                "mdi:sign-direction",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_relay_bitmap",
                                "继电器位图",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.relay }}",
                                NULL,
                                NULL,
                                "measurement",
                                "mdi:toggle-switch",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_fault_bitmap",
                                "故障位图",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.fault }}",
                                NULL,
                                NULL,
                                "measurement",
                                "mdi:alert-circle-outline",
                                NULL,
                                false,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_watchdog",
                                "看门狗刷新",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.wdg }}",
                                NULL,
                                NULL,
                                "total_increasing",
                                "mdi:watchdog",
                                NULL,
                                true,
                                true);
  NetworkPublishDiscoverySensor("industrial_controller_alive_bitmap",
                                "任务存活位图",
                                NETWORK_TELEMETRY_TOPIC,
                                "{{ value_json.alive }}",
                                NULL,
                                NULL,
                                "measurement",
                                "mdi:heart-pulse",
                                NULL,
                                true,
                                true);

  for (uint8_t index = 0U; index < 8U; index++)
  {
    (void)snprintf(object_id,
                   sizeof(object_id),
                   "industrial_controller_ai_%u",
                   (unsigned)index);
    (void)snprintf(name, sizeof(name), "AI%u 原始值", (unsigned)index);
    (void)snprintf(value_template,
                   sizeof(value_template),
                   "{{ value_json.ai_raw[%u] }}",
                   (unsigned)index);
    NetworkPublishDiscoverySensor(object_id,
                                  name,
                                  NETWORK_TELEMETRY_TOPIC,
                                  value_template,
                                  NULL,
                                  NULL,
                                  "measurement",
                                  "mdi:sine-wave",
                                  NULL,
                                  false,
                                  true);
  }

  NetworkPublishDiscoverySensor("industrial_controller_last_event",
                                "最近链路事件",
                                NETWORK_EVENT_TOPIC,
                                "{{ value_json.state }}",
                                NULL,
                                NULL,
                                NULL,
                                "mdi:history",
                                NETWORK_EVENT_TOPIC,
                                false,
                                false);
  NetworkPublishDiscoverySensor("industrial_controller_last_ack",
                                "最近命令结果",
                                NETWORK_COMMAND_ACK_TOPIC,
                                "{{ '成功' if value_json.result == 0 else '失败' }}",
                                NULL,
                                NULL,
                                NULL,
                                "mdi:check-circle-outline",
                                NETWORK_COMMAND_ACK_TOPIC,
                                false,
                                false);
  NetworkPublishDiscoverySensor("industrial_controller_last_bus_frame",
                                "最近现场总线帧",
                                NETWORK_BUS_TOPIC,
                                "{{ value_json.bus | upper }} x{{ value_json.length }}",
                                NULL,
                                NULL,
                                NULL,
                                "mdi:bus",
                                NETWORK_BUS_TOPIC,
                                true,
                                false);

  NetworkPublishDiscoveryBinarySensor("industrial_controller_gateway_online",
                                      "网关在线",
                                      NETWORK_AVAILABILITY_TOPIC,
                                      NULL,
                                      "online",
                                      "offline",
                                      "connectivity",
                                      "mdi:access-point-network",
                                      false,
                                      false);
  NetworkPublishDiscoveryBinarySensor("industrial_controller_fault_active",
                                      "存在故障",
                                      NETWORK_TELEMETRY_TOPIC,
                                      "{{ (value_json.fault | int(0)) > 0 }}",
                                      "True",
                                      "False",
                                      "problem",
                                      "mdi:alert",
                                      true,
                                      false);

  NetworkPublishDiscoveryButton("industrial_controller_all_relays_off",
                                "全部断开",
                                "mdi:power-off",
                                "{\"id\":1001,\"command\":\"relay\",\"mask\":0}");
  NetworkPublishDiscoveryButton("industrial_controller_all_relays_on",
                                "全部吸合",
                                "mdi:power",
                                "{\"id\":1002,\"command\":\"relay\",\"mask\":255}");
  NetworkPublishDiscoveryButton("industrial_controller_pulse_relay_1",
                                "通道1脉冲",
                                "mdi:timer",
                                "{\"id\":1003,\"command\":\"pulse\",\"channel\":0,"
                                "\"duration_ms\":1000}");
  NetworkPublishDiscoveryButton("industrial_controller_clear_faults",
                                "清除故障",
                                "mdi:eraser",
                                "{\"id\":1004,\"command\":\"clear\",\"mask\":65535}");
  NetworkPublishDiscoveryButton("industrial_controller_save_config",
                                "保存配置",
                                "mdi:content-save",
                                "{\"id\":1005,\"command\":\"save\"}");
  NetworkPublishDiscoveryButton("industrial_controller_load_config",
                                "加载配置",
                                "mdi:folder-open",
                                "{\"id\":1006,\"command\":\"load\"}");
  NetworkPublishDiscoveryNumber("industrial_controller_relay_mask",
                                "继电器掩码",
                                "mdi:dip-switch",
                                0.0,
                                255.0,
                                1.0,
                                relay_command_template,
                                "{{ value_json.relay }}");
}

static void NetworkPublishCommandResult(uint16_t request_id,
                                        uint16_t command_id,
                                        int result,
                                        uint16_t detail,
                                        const char *reason)
{
  char payload[256];
  char topic[96];
  uint32_t ack_sequence;
  int length;

  if ((s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    return;
  }

  ack_sequence = ++s_ack_sequence;

  if (reason != NULL)
  {
    length = snprintf(payload,
                      sizeof(payload),
                      "{\"id\":%u,\"command\":%u,\"result\":%d,"
                      "\"detail\":%u,\"seq\":%lu,\"reason\":\"%s\"}",
                      (unsigned)request_id,
                      (unsigned)command_id,
                      result,
                      (unsigned)detail,
                      (unsigned long)ack_sequence,
                      reason);
  }
  else
  {
    length = snprintf(payload,
                      sizeof(payload),
                      "{\"id\":%u,\"command\":%u,\"result\":%d,"
                      "\"detail\":%u,\"seq\":%lu}",
                      (unsigned)request_id,
                      (unsigned)command_id,
                      result,
                      (unsigned)detail,
                      (unsigned long)ack_sequence);
  }

  if (length <= 0)
  {
    return;
  }

  if (s_network_platform == NETWORK_PLATFORM_THINGSBOARD)
  {
    snprintf(topic,
             sizeof(topic),
             "%s%u",
             NETWORK_THINGSBOARD_RPC_RESPONSE_PREFIX,
             (unsigned)request_id);
    (void)esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
  }
  else
  {
    (void)esp_mqtt_client_publish(s_mqtt_client,
                                  NETWORK_COMMAND_ACK_TOPIC,
                                  payload,
                                  0,
                                  1,
                                  0);
  }
}

static void NetworkPublishCommandAck(const BridgeProtocolAck *ack)
{
  if (ack == NULL)
  {
    return;
  }

  NetworkPublishCommandResult(ack->request_id,
                              ack->command_id,
                              ack->result,
                              ack->detail,
                              NULL);
  NetworkSecurity_Record(ack->request_id,
                        ack->command_id,
                        ack->result,
                        ack->detail);
}

static const char *BusRxName(uint8_t bus)
{
  return (bus == BRIDGE_BUS_CAN) ? "can" : "rs232";
}

static void BusRxDataToHex(const BridgeProtocolBusRx *bus_rx,
                           char *output,
                           size_t output_size)
{
  size_t position = 0U;

  if ((bus_rx == NULL) || (output == NULL) || (output_size == 0U))
  {
    return;
  }

  output[0] = '\0';
  for (uint8_t index = 0U; index < bus_rx->length; index++)
  {
    int written = snprintf(&output[position],
                           output_size - position,
                           (index == 0U) ? "%02X" : " %02X",
                           bus_rx->data[index]);

    if ((written <= 0) || ((size_t)written >= (output_size - position)))
    {
      return;
    }
    position += (size_t)written;
  }
}

static void NetworkPublishBusFrame(const BridgeProtocolBusRx *bus_rx,
                                   uint32_t bus_frame_count)
{
  char payload[192];
  char hex[3U * BRIDGE_BUS_RX_MAX_DATA];
  int length;

  if ((bus_rx == NULL) || (s_mqtt_client == NULL) || !s_mqtt_connected ||
      (s_network_platform != NETWORK_PLATFORM_CUSTOM))
  {
    return;
  }

  BusRxDataToHex(bus_rx, hex, sizeof(hex));
  length = snprintf(payload,
                    sizeof(payload),
                    "{\"bus\":\"%s\",\"id\":%lu,\"length\":%u,"
                    "\"data\":\"%s\",\"count\":%lu}",
                    BusRxName(bus_rx->bus),
                    (unsigned long)bus_rx->identifier,
                    (unsigned)bus_rx->length,
                    hex,
                    (unsigned long)bus_frame_count);
  if (length <= 0)
  {
    return;
  }

  (void)esp_mqtt_client_publish(s_mqtt_client,
                                NETWORK_BUS_TOPIC,
                                payload,
                                0,
                                0,
                                0);
}

static bool JsonReadUint(const cJSON *root,
                         const char *name,
                         uint32_t maximum,
                         uint32_t *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
  uint32_t parsed;

  if ((item == NULL) || !cJSON_IsNumber(item) ||
      (item->valuedouble < 0.0) || (item->valuedouble > (double)maximum))
  {
    return false;
  }

  parsed = (uint32_t)item->valuedouble;
  if ((double)parsed != item->valuedouble)
  {
    return false;
  }

  *value = parsed;
  return true;
}

static bool JsonReadOptionalUint(const cJSON *root,
                                 const char *name,
                                 uint32_t maximum,
                                 uint32_t default_value,
                                 uint32_t *value)
{
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);

  if (item == NULL)
  {
    *value = default_value;
    return true;
  }

  return JsonReadUint(root, name, maximum, value);
}

static void NetworkRejectCommand(uint16_t request_id,
                                 uint16_t command_id,
                                 int result,
                                 const char *reason)
{
  NetworkPublishCommandResult(request_id, command_id, result, 0U, reason);
  NetworkSecurity_Record(request_id, command_id, result, 0U);
}

static bool NetworkCommandTokenValid(const cJSON *root)
{
  const cJSON *token;

  if (s_command_token[0] == '\0')
  {
    return true;
  }

  token = cJSON_GetObjectItemCaseSensitive(root, "token");
  return cJSON_IsString(token) &&
         (token->valuestring != NULL) &&
         (strcmp(token->valuestring, s_command_token) == 0);
}

static void HandleFrame(const BridgeProtocolFrame *frame)
{
  LinkHealth_OnValidFrame();

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

    case BRIDGE_MSG_BUS_RX:
    {
      BridgeProtocolBusRx bus_rx;

      if (BridgeProtocol_ParseBusRx(frame, &bus_rx))
      {
        uint32_t bus_frame_count;

        portENTER_CRITICAL(&s_state_lock);
        s_last_bus_rx = bus_rx;
        s_last_bus_rx_valid = true;
        s_bus_rx_bytes += bus_rx.length;
        if (bus_rx.bus == BRIDGE_BUS_CAN)
        {
          s_can_rx_frames++;
          bus_frame_count = s_can_rx_frames;
        }
        else
        {
          s_rs232_rx_frames++;
          bus_frame_count = s_rs232_rx_frames;
        }
        portEXIT_CRITICAL(&s_state_lock);

        ESP_LOGI(TAG,
                 "bus rx %s id=%lu length=%u",
                 BusRxName(bus_rx.bus),
                 (unsigned long)bus_rx.identifier,
                 (unsigned)bus_rx.length);
        NetworkPublishBusFrame(&bus_rx, bus_frame_count);
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
    LinkHealth_Poll();
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
  LinkHealthState last_link_state = LINK_HEALTH_BOOT;

  (void)argument;

  SendHello();

  for (;;)
  {
    LinkHealthSnapshot link;
    uint32_t uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    uint16_t length = BridgeProtocol_BuildHeartbeat(sequence++,
                                                    uptime_ms,
                                                    1U,
                                                    0U,
                                                    frame,
                                                    sizeof(frame));

    link = LinkHealth_GetSnapshot();
    if (link.state != last_link_state)
    {
      if (link.state == LINK_HEALTH_ONLINE)
      {
        if (last_link_state == LINK_HEALTH_OFFLINE)
        {
          ESP_LOGI(TAG, "STM32 link restored, reconnects=%lu",
                   (unsigned long)link.reconnect_count);
        }
        else
        {
          ESP_LOGI(TAG, "STM32 link online");
        }
      }
      else if (link.state == LINK_HEALTH_DEGRADED)
      {
        ESP_LOGW(TAG, "STM32 link degraded, age=%lums",
                 (unsigned long)link.last_rx_age_ms);
      }
      else if (link.state == LINK_HEALTH_OFFLINE)
      {
        ESP_LOGW(TAG, "STM32 link timeout");
      }
      last_link_state = link.state;
      NetworkPublishLinkEvent(&link);
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

static bool SubmitCommandWithId(uint16_t request_id,
                                uint16_t command_id,
                                uint32_t argument0,
                                uint32_t argument1,
                                uint32_t argument2)
{
  LinkHealthSnapshot link = LinkHealth_GetSnapshot();
  PendingCommand command = {
      .command_id = command_id,
      .argument0 = argument0,
      .argument1 = argument1,
      .argument2 = argument2,
  };

  if (!link.command_allowed)
  {
    printf("command blocked: STM32 link %s\n", LinkHealth_StateName(link.state));
    return false;
  }

  if (request_id == 0U)
  {
    s_request_id++;
    if (s_request_id == 0U)
    {
      s_request_id = 1U;
    }
    request_id = s_request_id;
  }
  command.request_id = request_id;

  if ((s_command_queue == NULL) ||
      (xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(100)) != pdTRUE))
  {
    printf("command queue full\n");
    return false;
  }

  printf("queued req=%u cmd=%04X\n", command.request_id, command.command_id);
  return true;
}

static bool SubmitCommand(uint16_t command_id,
                          uint32_t argument0,
                          uint32_t argument1,
                          uint32_t argument2)
{
  return SubmitCommandWithId(0U, command_id, argument0, argument1, argument2);
}

static void NetworkHandleCommand(const char *data, int length)
{
  cJSON *root;
  const cJSON *command;
  LinkHealthSnapshot link;
  uint16_t request_id = 0U;
  uint16_t command_id = 0U;
  uint32_t parsed_id;
  uint32_t argument0 = 0U;
  uint32_t argument1 = 0U;
  uint32_t argument2 = 0U;

  if ((data == NULL) || (length <= 0))
  {
    return;
  }

  root = cJSON_ParseWithLength(data, (size_t)length);
  if (root == NULL)
  {
    NetworkRejectCommand(0U,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "invalid JSON");
    return;
  }

  if (!cJSON_IsObject(root))
  {
    NetworkRejectCommand(0U,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "JSON object required");
    cJSON_Delete(root);
    return;
  }

  if (!JsonReadOptionalUint(root, "id", 65535U, 0U, &parsed_id))
  {
    NetworkRejectCommand(0U,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "invalid id");
    cJSON_Delete(root);
    return;
  }
  request_id = (uint16_t)parsed_id;

  command = cJSON_GetObjectItemCaseSensitive(root, "command");
  if (!cJSON_IsString(command) || (command->valuestring == NULL))
  {
    NetworkRejectCommand(request_id,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "command string required");
    cJSON_Delete(root);
    return;
  }

  if (!NetworkCommandTokenValid(root))
  {
    NetworkRejectCommand(request_id,
                         0U,
                         BRIDGE_RESULT_SAFETY_LOCK,
                         "invalid token");
    cJSON_Delete(root);
    return;
  }

  if (strcmp(command->valuestring, "relay") == 0)
  {
    command_id = BRIDGE_CMD_SET_RELAY_MASK;
    if (!JsonReadUint(root, "mask", 0xFFU, &argument0))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(command->valuestring, "pulse") == 0)
  {
    command_id = BRIDGE_CMD_PULSE_RELAY;
    if (!JsonReadUint(root, "channel", 7U, &argument0) ||
        !JsonReadUint(root, "duration_ms", 60000U, &argument1))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(command->valuestring, "dac") == 0)
  {
    command_id = BRIDGE_CMD_SET_ANALOG_OUTPUT;
    if (!JsonReadUint(root, "channel", 1U, &argument0) ||
        !JsonReadUint(root, "value", 4095U, &argument1))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(command->valuestring, "clear") == 0)
  {
    command_id = BRIDGE_CMD_CLEAR_FAULTS;
    if (!JsonReadUint(root, "mask", 0xFFFFU, &argument0))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(command->valuestring, "save") == 0)
  {
    command_id = BRIDGE_CMD_SAVE_CONFIG;
  }
  else if (strcmp(command->valuestring, "load") == 0)
  {
    command_id = BRIDGE_CMD_LOAD_CONFIG;
  }
  else
  {
    NetworkRejectCommand(request_id,
                         0U,
                         BRIDGE_RESULT_UNSUPPORTED,
                         "unsupported command");
    cJSON_Delete(root);
    return;
  }

  if (command_id == 0U)
  {
    NetworkRejectCommand(request_id,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "invalid argument");
    cJSON_Delete(root);
    return;
  }

  link = LinkHealth_GetSnapshot();
  if (!link.command_allowed)
  {
    NetworkRejectCommand(request_id,
                         command_id,
                         BRIDGE_RESULT_NOT_READY,
                         "STM32 link not online");
    cJSON_Delete(root);
    return;
  }

  if (!NetworkSecurity_AllowOutputCommand(command_id))
  {
    NetworkRejectCommand(request_id,
                         command_id,
                         BRIDGE_RESULT_BUSY,
                         "rate limited");
    cJSON_Delete(root);
    return;
  }

  if (!SubmitCommandWithId(request_id,
                           command_id,
                           argument0,
                           argument1,
                           argument2))
  {
    NetworkRejectCommand(request_id,
                         command_id,
                         BRIDGE_RESULT_BUSY,
                         "command queue full");
    cJSON_Delete(root);
    return;
  }

  cJSON_Delete(root);
}

static void ThingsBoardHandleRpc(const char *topic,
                                 const char *data,
                                 int length)
{
  cJSON *root;
  const cJSON *method;
  const cJSON *params;
  char *end = NULL;
  unsigned long request_id;
  uint16_t command_id = 0U;
  uint32_t argument0 = 0U;
  uint32_t argument1 = 0U;
  LinkHealthSnapshot link;

  if ((topic == NULL) || (data == NULL) || (length <= 0) ||
      (strncmp(topic,
               NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX,
               strlen(NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX)) != 0))
  {
    return;
  }

  request_id = strtoul(topic + strlen(NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX),
                       &end,
                       10);
  if ((end == topic + strlen(NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX)) ||
      (*end != '\0') || (request_id > 65535UL))
  {
    return;
  }

  root = cJSON_ParseWithLength(data, (size_t)length);
  if (root == NULL)
  {
    NetworkRejectCommand((uint16_t)request_id,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "invalid JSON");
    return;
  }

  method = cJSON_GetObjectItemCaseSensitive(root, "method");
  params = cJSON_GetObjectItemCaseSensitive(root, "params");
  if (!cJSON_IsString(method) || (method->valuestring == NULL))
  {
    NetworkRejectCommand((uint16_t)request_id,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "method required");
    cJSON_Delete(root);
    return;
  }

  if ((strcmp(method->valuestring, "relay") == 0) ||
      (strcmp(method->valuestring, "setRelay") == 0))
  {
    command_id = BRIDGE_CMD_SET_RELAY_MASK;
    if (cJSON_IsNumber(params))
    {
      if ((params->valuedouble < 0.0) || (params->valuedouble > 255.0))
      {
        command_id = 0U;
      }
      else
      {
        argument0 = (uint32_t)params->valuedouble;
      }
    }
    else if (!JsonReadUint(params, "mask", 0xFFU, &argument0))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(method->valuestring, "pulse") == 0)
  {
    command_id = BRIDGE_CMD_PULSE_RELAY;
    if (!JsonReadUint(params, "channel", 7U, &argument0) ||
        !JsonReadUint(params, "duration_ms", 60000U, &argument1))
    {
      command_id = 0U;
    }
  }
  else if ((strcmp(method->valuestring, "dac") == 0) ||
           (strcmp(method->valuestring, "setDac") == 0))
  {
    command_id = BRIDGE_CMD_SET_ANALOG_OUTPUT;
    if (!JsonReadUint(params, "channel", 1U, &argument0) ||
        !JsonReadUint(params, "value", 4095U, &argument1))
    {
      command_id = 0U;
    }
  }
  else if ((strcmp(method->valuestring, "clear") == 0) ||
           (strcmp(method->valuestring, "clearFaults") == 0))
  {
    command_id = BRIDGE_CMD_CLEAR_FAULTS;
    if (cJSON_IsNumber(params))
    {
      if ((params->valuedouble < 0.0) || (params->valuedouble > 65535.0))
      {
        command_id = 0U;
      }
      else
      {
        argument0 = (uint32_t)params->valuedouble;
      }
    }
    else if (!JsonReadUint(params, "mask", 0xFFFFU, &argument0))
    {
      command_id = 0U;
    }
  }
  else if (strcmp(method->valuestring, "save") == 0)
  {
    command_id = BRIDGE_CMD_SAVE_CONFIG;
  }
  else if (strcmp(method->valuestring, "load") == 0)
  {
    command_id = BRIDGE_CMD_LOAD_CONFIG;
  }
  else
  {
    NetworkRejectCommand((uint16_t)request_id,
                         0U,
                         BRIDGE_RESULT_UNSUPPORTED,
                         "unsupported method");
    cJSON_Delete(root);
    return;
  }

  if (command_id == 0U)
  {
    NetworkRejectCommand((uint16_t)request_id,
                         0U,
                         BRIDGE_RESULT_INVALID_ARGUMENT,
                         "invalid params");
    cJSON_Delete(root);
    return;
  }

  link = LinkHealth_GetSnapshot();
  if (!link.command_allowed)
  {
    NetworkRejectCommand((uint16_t)request_id,
                         command_id,
                         BRIDGE_RESULT_NOT_READY,
                         "STM32 link not online");
    cJSON_Delete(root);
    return;
  }

  if (!NetworkSecurity_AllowOutputCommand(command_id))
  {
    NetworkRejectCommand((uint16_t)request_id,
                         command_id,
                         BRIDGE_RESULT_BUSY,
                         "rate limited");
    cJSON_Delete(root);
    return;
  }

  if (!SubmitCommandWithId((uint16_t)request_id,
                           command_id,
                           argument0,
                           argument1,
                           0U))
  {
    NetworkRejectCommand((uint16_t)request_id,
                         command_id,
                         BRIDGE_RESULT_BUSY,
                         "command queue full");
  }

  cJSON_Delete(root);
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
  LinkHealthSnapshot link;
  bool diagnostics_valid;

  link = LinkHealth_GetSnapshot();
  portENTER_CRITICAL(&s_state_lock);
  telemetry = s_telemetry;
  diagnostics = s_diagnostics;
  diagnostics_valid = s_diagnostics_valid;
  portEXIT_CRITICAL(&s_state_lock);

  printf("STM32: %s\n", LinkHealth_StateName(link.state));
  printf("LINK_AGE=%lums RECONNECTS=%lu RX_FRAMES=%lu TIMEOUTS=%lu COMMAND_GATE=%s\n",
         (unsigned long)link.last_rx_age_ms,
         (unsigned long)link.reconnect_count,
         (unsigned long)link.rx_frames,
         (unsigned long)link.timeout_count,
         link.command_allowed ? "allowed" : "blocked");
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
  printf("MQTT_AUTH=%s COMMAND_TOKEN=%s\n",
         (s_mqtt_user[0] != '\0') ? "configured" : "disabled",
         (s_command_token[0] != '\0') ? "configured" : "disabled");
  printf("WEB_AUTH=%s\n",
         (s_web_user[0] != '\0') ? "configured" : "disabled");
  printf("PLATFORM=%s\n",
         (s_network_platform == NETWORK_PLATFORM_THINGSBOARD)
             ? "thingsboard"
             : "custom");
}

static void NetworkLoadConfig(void)
{
  nvs_handle_t handle;
  size_t length;
  uint8_t platform = NETWORK_PLATFORM_CUSTOM;

  memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
  memset(s_wifi_password, 0, sizeof(s_wifi_password));
  memset(s_mqtt_broker, 0, sizeof(s_mqtt_broker));
  memset(s_mqtt_user, 0, sizeof(s_mqtt_user));
  memset(s_mqtt_password, 0, sizeof(s_mqtt_password));
  memset(s_command_token, 0, sizeof(s_command_token));
  memset(s_web_user, 0, sizeof(s_web_user));
  memset(s_web_password, 0, sizeof(s_web_password));

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
  length = sizeof(s_mqtt_user);
  (void)nvs_get_str(handle, NETWORK_NVS_MQTT_USER, s_mqtt_user, &length);
  length = sizeof(s_mqtt_password);
  (void)nvs_get_str(handle, NETWORK_NVS_MQTT_PASSWORD, s_mqtt_password, &length);
  length = sizeof(s_command_token);
  (void)nvs_get_str(handle, NETWORK_NVS_COMMAND_TOKEN, s_command_token, &length);
  length = sizeof(s_web_user);
  (void)nvs_get_str(handle, NETWORK_NVS_WEB_USER, s_web_user, &length);
  length = sizeof(s_web_password);
  (void)nvs_get_str(handle, NETWORK_NVS_WEB_PASSWORD, s_web_password, &length);
  length = sizeof(platform);
  if (nvs_get_u8(handle, NETWORK_NVS_PLATFORM, &platform) != ESP_OK)
  {
    platform = NETWORK_PLATFORM_CUSTOM;
  }
  nvs_close(handle);

  s_network_platform = (platform == NETWORK_PLATFORM_THINGSBOARD)
                           ? NETWORK_PLATFORM_THINGSBOARD
                           : NETWORK_PLATFORM_CUSTOM;
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

static void NetworkSaveMqttAuth(const char *username, const char *password)
{
  nvs_handle_t handle;

  if ((username == NULL) || (password == NULL))
  {
    return;
  }

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_str(handle, NETWORK_NVS_MQTT_USER, username);
  (void)nvs_set_str(handle, NETWORK_NVS_MQTT_PASSWORD, password);
  (void)nvs_commit(handle);
  nvs_close(handle);
  s_network_reload = true;
  printf("MQTT credentials saved\n");
}

static void NetworkSaveCommandToken(const char *token)
{
  nvs_handle_t handle;

  if (token == NULL)
  {
    return;
  }

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_str(handle, NETWORK_NVS_COMMAND_TOKEN, token);
  (void)nvs_commit(handle);
  nvs_close(handle);
  strncpy(s_command_token, token, sizeof(s_command_token) - 1U);
  s_command_token[sizeof(s_command_token) - 1U] = '\0';
  printf("command token saved\n");
}

static void NetworkSaveWebAuth(const char *username, const char *password)
{
  nvs_handle_t handle;

  if ((username == NULL) || (password == NULL))
  {
    return;
  }

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_str(handle, NETWORK_NVS_WEB_USER, username);
  (void)nvs_set_str(handle, NETWORK_NVS_WEB_PASSWORD, password);
  (void)nvs_commit(handle);
  nvs_close(handle);
  strncpy(s_web_user, username, sizeof(s_web_user) - 1U);
  s_web_user[sizeof(s_web_user) - 1U] = '\0';
  strncpy(s_web_password, password, sizeof(s_web_password) - 1U);
  s_web_password[sizeof(s_web_password) - 1U] = '\0';
  printf("web credentials saved\n");
}

static bool NetworkWebAuthenticate(const char *username, const char *password)
{
  if (s_web_user[0] == '\0')
  {
    return true;
  }

  if ((username == NULL) || (password == NULL))
  {
    return false;
  }

  return (strcmp(username, s_web_user) == 0) &&
         (strcmp(password, s_web_password) == 0);
}

static void NetworkSavePlatform(NetworkPlatform platform)
{
  nvs_handle_t handle;
  uint8_t value = (uint8_t)platform;

  if (nvs_open(NETWORK_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
  {
    printf("NVS open failed\n");
    return;
  }

  (void)nvs_set_u8(handle, NETWORK_NVS_PLATFORM, value);
  (void)nvs_commit(handle);
  nvs_close(handle);
  s_network_platform = platform;
}

static void NetworkStartMqtt(void)
{
  esp_mqtt_client_config_t mqtt_config = {
      .broker.address.uri = s_mqtt_broker,
      .credentials.username =
          (s_mqtt_user[0] != '\0') ? s_mqtt_user : NULL,
      .credentials.authentication.password =
          (s_mqtt_password[0] != '\0') ? s_mqtt_password : NULL,
      .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
      .buffer.size = 2048,
      .buffer.out_size = 2048,
  };

  if (s_network_platform == NETWORK_PLATFORM_CUSTOM)
  {
    mqtt_config.session.last_will.topic = NETWORK_AVAILABILITY_TOPIC;
    mqtt_config.session.last_will.msg = "offline";
    mqtt_config.session.last_will.msg_len = 7;
    mqtt_config.session.last_will.qos = 1;
    mqtt_config.session.last_will.retain = 1;
  }

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

  if ((s_network_platform == NETWORK_PLATFORM_CUSTOM) && s_mqtt_connected)
  {
    (void)esp_mqtt_client_publish(s_mqtt_client,
                                  NETWORK_AVAILABILITY_TOPIC,
                                  "offline",
                                  0,
                                  1,
                                  1);
    vTaskDelay(pdMS_TO_TICKS(20));
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
    LinkHealthSnapshot link = LinkHealth_GetSnapshot();

    s_mqtt_connected = true;
    ESP_LOGI(TAG, "MQTT connected");
    if (s_network_platform == NETWORK_PLATFORM_THINGSBOARD)
    {
      (void)esp_mqtt_client_subscribe(s_mqtt_client,
                                      NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX "+",
                                      1);
    }
    else
    {
      NetworkPublishHomeAssistantDiscovery();
      (void)esp_mqtt_client_subscribe(s_mqtt_client, NETWORK_COMMAND_TOPIC, 1);
    }
    NetworkPublishLinkEvent(&link);
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
  else if (event_id == MQTT_EVENT_DATA)
  {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    if ((event != NULL) && (event->topic != NULL) && (event->data != NULL) &&
        (s_network_platform == NETWORK_PLATFORM_THINGSBOARD) &&
        (event->topic_len >= (int)strlen(NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX)) &&
        (strncmp(event->topic,
                 NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX,
                 strlen(NETWORK_THINGSBOARD_RPC_REQUEST_PREFIX)) == 0))
    {
      ThingsBoardHandleRpc(event->topic, event->data, event->data_len);
    }
    else if ((event != NULL) && (event->topic != NULL) && (event->data != NULL) &&
             (event->topic_len == (int)strlen(NETWORK_COMMAND_TOPIC)) &&
             (strncmp(event->topic,
                      NETWORK_COMMAND_TOPIC,
                      (size_t)event->topic_len) == 0))
    {
      NetworkHandleCommand(event->data, event->data_len);
    }
  }
}

static void NetworkPublishTelemetry(void)
{
  BridgeProtocolTelemetry telemetry;
  BridgeProtocolDiagnostics diagnostics;
  LinkHealthSnapshot link;
  bool diagnostics_valid;
  char payload[512];
  int position = 0;

  if ((s_mqtt_client == NULL) || !s_mqtt_connected)
  {
    return;
  }

  link = LinkHealth_GetSnapshot();
  portENTER_CRITICAL(&s_state_lock);
  telemetry = s_telemetry;
  diagnostics = s_diagnostics;
  diagnostics_valid = s_diagnostics_valid;
  portEXIT_CRITICAL(&s_state_lock);

  if (s_network_platform == NETWORK_PLATFORM_THINGSBOARD)
  {
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         "{\"ai0\":%ld,\"ai1\":%ld,\"ai2\":%ld,\"ai3\":%ld,"
                         "\"ai4\":%ld,\"ai5\":%ld,\"ai6\":%ld,\"ai7\":%ld",
                         (long)telemetry.ai_raw[0],
                         (long)telemetry.ai_raw[1],
                         (long)telemetry.ai_raw[2],
                         (long)telemetry.ai_raw[3],
                         (long)telemetry.ai_raw[4],
                         (long)telemetry.ai_raw[5],
                         (long)telemetry.ai_raw[6],
                         (long)telemetry.ai_raw[7]);
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         ",\"rtd_mc\":%ld,\"di\":%u,\"relay\":%u,"
                         "\"supply24_mv\":%u,\"supply5_mv\":%u,\"fault\":%u",
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
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         ",\"link_state\":\"%s\",\"link_age_ms\":%lu,"
                         "\"reconnects\":%lu,\"timeouts\":%lu",
                         LinkHealth_StateName(link.state),
                         (unsigned long)link.last_rx_age_ms,
                         (unsigned long)link.reconnect_count,
                         (unsigned long)link.timeout_count);
    (void)snprintf(&payload[position],
                   sizeof(payload) - (size_t)position,
                   "}");
    (void)esp_mqtt_client_publish(s_mqtt_client,
                                  NETWORK_THINGSBOARD_TELEMETRY_TOPIC,
                                  payload,
                                  0,
                                  0,
                                  0);
  }
  else
  {
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
    position += snprintf(&payload[position],
                         sizeof(payload) - (size_t)position,
                         ",\"link_state\":\"%s\",\"link_age_ms\":%lu,"
                         "\"reconnects\":%lu,\"timeouts\":%lu",
                         LinkHealth_StateName(link.state),
                         (unsigned long)link.last_rx_age_ms,
                         (unsigned long)link.reconnect_count,
                         (unsigned long)link.timeout_count);
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

static void PrintLinkEvents(void)
{
  LinkHealthEvent events[LINK_HEALTH_EVENT_HISTORY_SIZE];
  uint8_t count = LinkHealth_GetEvents(events, LINK_HEALTH_EVENT_HISTORY_SIZE);

  if (count == 0U)
  {
    printf("no link events\n");
    return;
  }

  for (uint8_t index = 0U; index < count; index++)
  {
    printf("LINK %lums %-8s reconnects=%lu timeouts=%lu\n",
           (unsigned long)events[index].uptime_ms,
           LinkHealth_StateName(events[index].state),
           (unsigned long)events[index].reconnect_count,
           (unsigned long)events[index].timeout_count);
  }
}

static void PrintCommandAudit(void)
{
  NetworkSecurityAuditEntry entries[NETWORK_SECURITY_AUDIT_SIZE];
  uint8_t count = NetworkSecurity_GetAudit(entries, NETWORK_SECURITY_AUDIT_SIZE);

  if (count == 0U)
  {
    printf("no command audit entries\n");
    return;
  }

  for (uint8_t index = 0U; index < count; index++)
  {
    printf("AUDIT %lums id=%u cmd=%04X result=%d detail=%u\n",
           (unsigned long)entries[index].uptime_ms,
           (unsigned)entries[index].request_id,
           (unsigned)entries[index].command_id,
           entries[index].result,
           (unsigned)entries[index].detail);
  }
}

static void WebAppend(char *output,
                      size_t output_size,
                      int *position,
                      const char *format,
                      ...)
{
  va_list arguments;
  int written;

  if ((output == NULL) || (position == NULL) ||
      (*position < 0) || ((size_t)*position >= output_size))
  {
    return;
  }

  va_start(arguments, format);
  written = vsnprintf(&output[*position],
                      output_size - (size_t)*position,
                      format,
                      arguments);
  va_end(arguments);

  if (written > 0)
  {
    *position += written;
    if ((size_t)*position >= output_size)
    {
      *position = (int)output_size - 1;
    }
  }
}

static void WebBuildStatusJson(char *output, size_t output_size)
{
  BridgeProtocolTelemetry telemetry;
  BridgeProtocolDiagnostics diagnostics;
  LinkHealthSnapshot link;
  bool diagnostics_valid;
  int position = 0;

  if ((output == NULL) || (output_size == 0U))
  {
    return;
  }

  link = LinkHealth_GetSnapshot();
  portENTER_CRITICAL(&s_state_lock);
  telemetry = s_telemetry;
  diagnostics = s_diagnostics;
  diagnostics_valid = s_diagnostics_valid;
  portEXIT_CRITICAL(&s_state_lock);

  WebAppend(output, output_size, &position, "{\"link_state\":\"%s\",",
            LinkHealth_StateName(link.state));
  WebAppend(output, output_size, &position,
            "\"link_age_ms\":%lu,\"reconnects\":%lu,\"timeouts\":%lu,",
            (unsigned long)link.last_rx_age_ms,
            (unsigned long)link.reconnect_count,
            (unsigned long)link.timeout_count);
  WebAppend(output, output_size, &position,
            "\"command_allowed\":%s,\"wifi_connected\":%s,"
            "\"mqtt_connected\":%s,\"mqtt_auth_configured\":%s,"
            "\"command_token_configured\":%s,\"broker\":\"%s\",",
            link.command_allowed ? "true" : "false",
            s_wifi_connected ? "true" : "false",
            s_mqtt_connected ? "true" : "false",
            (s_mqtt_user[0] != '\0') ? "true" : "false",
            (s_command_token[0] != '\0') ? "true" : "false",
            s_mqtt_broker);
  WebAppend(output, output_size, &position, "\"ai_raw\":[");
  for (uint8_t index = 0U; index < 8U; index++)
  {
    WebAppend(output,
              output_size,
              &position,
              "%s%ld",
              (index == 0U) ? "" : ",",
              (long)telemetry.ai_raw[index]);
  }
  WebAppend(output, output_size, &position,
            "],\"rtd_millicelsius\":%ld,\"di_bits\":%u,"
            "\"relay_bits\":%u,\"supply_mv\":[%u,%u],"
            "\"fault_bits\":%u",
            (long)telemetry.rtd_millicelsius,
            telemetry.di_bits,
            telemetry.relay_bits,
            telemetry.supply_mv[0],
            telemetry.supply_mv[1],
            telemetry.fault_bits);
  if (diagnostics_valid)
  {
    WebAppend(output, output_size, &position,
              ",\"alive_bits\":%lu,\"watchdog_refresh_count\":%lu",
              (unsigned long)diagnostics.task_alive_bits,
              (unsigned long)diagnostics.watchdog_refresh_count);
  }
  else
  {
    WebAppend(output, output_size, &position,
              ",\"alive_bits\":0,\"watchdog_refresh_count\":0");
  }

  {
    BridgeProtocolBusRx bus_rx;
    bool bus_valid;
    uint32_t rs232_frames;
    uint32_t can_frames;
    uint32_t bus_bytes;
    char hex[3U * BRIDGE_BUS_RX_MAX_DATA];

    portENTER_CRITICAL(&s_state_lock);
    bus_rx = s_last_bus_rx;
    bus_valid = s_last_bus_rx_valid;
    rs232_frames = s_rs232_rx_frames;
    can_frames = s_can_rx_frames;
    bus_bytes = s_bus_rx_bytes;
    portEXIT_CRITICAL(&s_state_lock);

    WebAppend(output,
              output_size,
              &position,
              ",\"rs232_rx_frames\":%lu,\"can_rx_frames\":%lu,\"bus_rx_bytes\":%lu",
              (unsigned long)rs232_frames,
              (unsigned long)can_frames,
              (unsigned long)bus_bytes);

    if (bus_valid)
    {
      BusRxDataToHex(&bus_rx, hex, sizeof(hex));
      WebAppend(output,
                output_size,
                &position,
                ",\"last_bus\":\"%s\",\"last_bus_id\":%lu,"
                "\"last_bus_length\":%u,\"last_bus_data\":\"%s\"",
                BusRxName(bus_rx.bus),
                (unsigned long)bus_rx.identifier,
                (unsigned)bus_rx.length,
                hex);
    }
    else
    {
      WebAppend(output,
                output_size,
                &position,
                ",\"last_bus\":null,\"last_bus_id\":0,"
                "\"last_bus_length\":0,\"last_bus_data\":\"\"");
    }
  }

  WebAppend(output, output_size, &position, "}");
}

static void WebBuildEventsJson(char *output, size_t output_size)
{
  LinkHealthEvent events[LINK_HEALTH_EVENT_HISTORY_SIZE];
  uint8_t count = LinkHealth_GetEvents(events, LINK_HEALTH_EVENT_HISTORY_SIZE);
  int position = 0;

  if ((output == NULL) || (output_size == 0U))
  {
    return;
  }

  WebAppend(output, output_size, &position, "[");
  for (uint8_t index = 0U; index < count; index++)
  {
    WebAppend(output,
              output_size,
              &position,
              "%s{\"uptime_ms\":%lu,\"state\":\"%s\","
              "\"reconnect_count\":%lu,\"timeout_count\":%lu}",
              (index == 0U) ? "" : ",",
              (unsigned long)events[index].uptime_ms,
              LinkHealth_StateName(events[index].state),
              (unsigned long)events[index].reconnect_count,
              (unsigned long)events[index].timeout_count);
  }
  WebAppend(output, output_size, &position, "]");
}

static void WebBuildAuditJson(char *output, size_t output_size)
{
  NetworkSecurityAuditEntry entries[NETWORK_SECURITY_AUDIT_SIZE];
  uint8_t count = NetworkSecurity_GetAudit(entries, NETWORK_SECURITY_AUDIT_SIZE);
  int position = 0;

  if ((output == NULL) || (output_size == 0U))
  {
    return;
  }

  WebAppend(output, output_size, &position, "[");
  for (uint8_t index = 0U; index < count; index++)
  {
    WebAppend(output,
              output_size,
              &position,
              "%s{\"uptime_ms\":%lu,\"request_id\":%u,"
              "\"command_id\":%u,\"result\":%d,\"detail\":%u}",
              (index == 0U) ? "" : ",",
              (unsigned long)entries[index].uptime_ms,
              (unsigned)entries[index].request_id,
              (unsigned)entries[index].command_id,
              entries[index].result,
              (unsigned)entries[index].detail);
  }
  WebAppend(output, output_size, &position, "]");
}

static bool WebCommandBridge(const char *payload, int length)
{
  if ((payload == NULL) || (length <= 0))
  {
    return false;
  }

  NetworkHandleCommand(payload, length);
  return true;
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
      printf("events              print recent link state events\n");
      printf("audit               print recent command audit entries\n");
      printf("wifi <ssid> <pass>  save WiFi credentials and reconnect\n");
      printf("mqtt <uri>          save MQTT broker URI\n");
      printf("mqttauth <user> <pass> save MQTT credentials\n");
      printf("mqttauth off        disable MQTT credentials\n");
      printf("token <value>       set MQTT command token\n");
      printf("token off           disable MQTT command token\n");
      printf("webuser <user> <pass> set Web console credentials\n");
      printf("webuser off         disable Web console login\n");
      printf("tb <access-token>   use ThingsBoard Cloud\n");
      printf("tb off              return to custom MQTT protocol\n");
      printf("reconnect           apply saved network configuration\n");
    }
    else if (strcmp(command, "status") == 0)
    {
      PrintStatus();
    }
    else if (strcmp(command, "events") == 0)
    {
      PrintLinkEvents();
    }
    else if (strcmp(command, "audit") == 0)
    {
      PrintCommandAudit();
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
    else if ((strcmp(command, "mqttauth") == 0) &&
             (argument1 != NULL) &&
             (strcmp(argument1, "off") == 0))
    {
      NetworkSaveMqttAuth("", "");
    }
    else if ((strcmp(command, "mqttauth") == 0) &&
             (argument1 != NULL) &&
             (argument2 != NULL))
    {
      NetworkSaveMqttAuth(argument1, argument2);
    }
    else if ((strcmp(command, "token") == 0) &&
             (argument1 != NULL) &&
             (strcmp(argument1, "off") == 0))
    {
      NetworkSaveCommandToken("");
    }
    else if ((strcmp(command, "token") == 0) && (argument1 != NULL))
    {
      NetworkSaveCommandToken(argument1);
    }
    else if ((strcmp(command, "webuser") == 0) &&
             (argument1 != NULL) &&
             (strcmp(argument1, "off") == 0))
    {
      NetworkSaveWebAuth("", "");
    }
    else if ((strcmp(command, "webuser") == 0) &&
             (argument1 != NULL) &&
             (argument2 != NULL))
    {
      NetworkSaveWebAuth(argument1, argument2);
    }
    else if ((strcmp(command, "tb") == 0) &&
             (argument1 != NULL) &&
             (strcmp(argument1, "off") == 0))
    {
      NetworkSavePlatform(NETWORK_PLATFORM_CUSTOM);
      printf("platform=custom\n");
    }
    else if ((strcmp(command, "tb") == 0) && (argument1 != NULL))
    {
      NetworkSaveBroker(NETWORK_THINGSBOARD_BROKER);
      NetworkSaveMqttAuth(argument1, "");
      NetworkSavePlatform(NETWORK_PLATFORM_THINGSBOARD);
      printf("platform=thingsboard\n");
    }
    else if (strcmp(command, "tb") == 0)
    {
      printf("platform=%s\n",
             (s_network_platform == NETWORK_PLATFORM_THINGSBOARD)
                 ? "thingsboard"
                 : "custom");
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
    LinkHealthSnapshot link = LinkHealth_GetSnapshot();

    if (!link.command_allowed)
    {
      printf("req=%u rejected: STM32 link %s\n",
             command.request_id,
             LinkHealth_StateName(link.state));
      NetworkRejectCommand(command.request_id,
                           command.command_id,
                           BRIDGE_RESULT_NOT_READY,
                           "STM32 link not online");
      continue;
    }

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
      NetworkPublishCommandAck(&ack);
    }
    else
    {
      printf("req=%u no ACK\n", command.request_id);
      NetworkRejectCommand(command.request_id,
                           command.command_id,
                           BRIDGE_RESULT_NOT_READY,
                           "no ACK");
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

  LinkHealth_Init();
  NetworkSecurity_Init();
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

  if (!WebConsole_Start(WebBuildStatusJson,
                        WebBuildEventsJson,
                        WebBuildAuditJson,
                        WebCommandBridge,
                        NetworkWebAuthenticate))
  {
    ESP_LOGE(TAG, "web console unavailable");
  }
}
