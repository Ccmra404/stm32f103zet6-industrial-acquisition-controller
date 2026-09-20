#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_PORT UART_NUM_1
#define UART_TX_GPIO GPIO_NUM_17
#define UART_RX_GPIO GPIO_NUM_18
#define UART_BAUD_RATE 115200U
#define UART_RX_BUFFER_SIZE 256U
#define UART_TX_BUFFER_SIZE 256U
#define LINE_BUFFER_SIZE 64U

static const char *TAG = "uart_validation";

static void SendText(const char *text)
{
  if (text != NULL)
  {
    (void)uart_write_bytes(UART_PORT, text, strlen(text));
  }
}

void app_main(void)
{
  char line[LINE_BUFFER_SIZE];
  size_t line_length = 0U;
  TickType_t last_ping = 0U;

  uart_config_t config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));
  ESP_ERROR_CHECK(uart_set_pin(UART_PORT,
                               UART_TX_GPIO,
                               UART_RX_GPIO,
                               UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(UART_PORT,
                                      UART_RX_BUFFER_SIZE,
                                      UART_TX_BUFFER_SIZE,
                                      0,
                                      NULL,
                                      0));

  ESP_LOGI(TAG, "UART validation started");

  for (;;)
  {
    uint8_t byte;
    int received = uart_read_bytes(UART_PORT, &byte, 1U, pdMS_TO_TICKS(50));

    if (received == 1)
    {
      if ((byte == '\r') || (byte == '\n'))
      {
        if (line_length > 0U)
        {
          line[line_length] = '\0';
          ESP_LOGI(TAG, "RX: %s", line);
          if (strcmp(line, "PING") == 0)
          {
            SendText("PONG\r\n");
          }
          line_length = 0U;
        }
      }
      else if (line_length < (LINE_BUFFER_SIZE - 1U))
      {
        line[line_length++] = (char)byte;
      }
    }

    if ((xTaskGetTickCount() - last_ping) >= pdMS_TO_TICKS(1000U))
    {
      last_ping = xTaskGetTickCount();
      SendText("PING\r\n");
    }
  }
}
