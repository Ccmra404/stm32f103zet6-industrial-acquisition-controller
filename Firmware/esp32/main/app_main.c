#include <stdbool.h>
#include <stdint.h>
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
#define BRIDGE_COMMAND_PERIOD_MS 1000U

static const char *TAG = "industrial_bridge";

static BridgeProtocolParser s_parser;
static BridgeProtocolTelemetry s_telemetry;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_stm_online;
static int64_t s_last_rx_us;

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

static void CommandRouterTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    vTaskDelay(pdMS_TO_TICKS(BRIDGE_COMMAND_PERIOD_MS));
  }
}

void app_main(void)
{
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

  xTaskCreate(UartRxTask, "uart_rx", 4096, NULL, 8, NULL);
  xTaskCreate(HeartbeatTask, "heartbeat", 3072, NULL, 6, NULL);
  xTaskCreate(CommandRouterTask, "command_router", 3072, NULL, 6, NULL);
}
