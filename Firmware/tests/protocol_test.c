#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bridge_protocol.h"

static int s_checks;
static int s_failures;

#define CHECK(condition)                                                        \
  do                                                                            \
  {                                                                             \
    s_checks++;                                                                 \
    if (!(condition))                                                           \
    {                                                                           \
      s_failures++;                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
      return false;                                                             \
    }                                                                           \
  } while (0)

static bool ParseBytes(const uint8_t *data, uint16_t length, BridgeProtocolFrame *frame)
{
  BridgeProtocolParser parser;

  BridgeProtocol_ParserInit(&parser);
  for (uint16_t index = 0U; index < length; index++)
  {
    if (BridgeProtocol_ParserPushByte(&parser, data[index], frame))
    {
      return true;
    }
  }

  return false;
}

static bool TestCrcKnownVector(void)
{
  static const uint8_t input[] = "123456789";

  CHECK(BridgeProtocol_Crc16(input, (uint16_t)(sizeof(input) - 1U)) == 0x29B1U);
  return true;
}

static bool TestHeartbeatRoundTrip(void)
{
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolHeartbeat heartbeat;
  uint16_t length = BridgeProtocol_BuildHeartbeat(0x1234U,
                                                  0x11223344U,
                                                  0x0003U,
                                                  0x0040U,
                                                  frame_data,
                                                  sizeof(frame_data));

  CHECK(length == 18U);
  CHECK(frame_data[0] == 0xAAU);
  CHECK(frame_data[1] == 0x55U);
  CHECK(frame_data[2] == BRIDGE_PROTOCOL_VERSION);
  CHECK(frame_data[3] == BRIDGE_MSG_HEARTBEAT);
  CHECK(frame_data[4] == 0x34U);
  CHECK(frame_data[5] == 0x12U);
  CHECK(frame_data[8] == 0x44U);
  CHECK(frame_data[9] == 0x33U);
  CHECK(frame_data[10] == 0x22U);
  CHECK(frame_data[11] == 0x11U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseHeartbeat(&frame, &heartbeat));
  CHECK(heartbeat.uptime_ms == 0x11223344U);
  CHECK(heartbeat.health_flags == 0x0003U);
  CHECK(heartbeat.fault_bits == 0x0040U);
  return true;
}

static bool TestTelemetryRoundTrip(void)
{
  const int32_t inputs[8] = {
      -100,
      0,
      100,
      0x123456,
      -0x123456,
      7,
      8,
      9,
  };
  const uint16_t supplies[2] = {24000U, 5000U};
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolTelemetry telemetry;
  uint16_t length = BridgeProtocol_BuildTelemetry(7U,
                                                  123456U,
                                                  inputs,
                                                  -1234,
                                                  0xA5U,
                                                  0x5AU,
                                                  supplies,
                                                  0x0004U,
                                                  frame_data,
                                                  sizeof(frame_data));

  CHECK(length == 58U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseTelemetry(&frame, &telemetry));
  CHECK(telemetry.timestamp_ms == 123456U);
  CHECK(telemetry.rtd_millicelsius == -1234);
  CHECK(telemetry.di_bits == 0xA5U);
  CHECK(telemetry.relay_bits == 0x5AU);
  CHECK(telemetry.supply_mv[0] == 24000U);
  CHECK(telemetry.supply_mv[1] == 5000U);
  CHECK(telemetry.fault_bits == 0x0004U);
  for (uint8_t index = 0U; index < 8U; index++)
  {
    CHECK(telemetry.ai_raw[index] == inputs[index]);
  }
  return true;
}

static bool TestDiagnosticsRoundTrip(void)
{
  const uint16_t stack_free[BRIDGE_DIAGNOSTIC_TASK_COUNT] = {200U, 300U, 400U, 500U, 600U, 700U};
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolDiagnostics diagnostics;
  uint16_t length = BridgeProtocol_BuildDiagnostics(8U,
                                                    9000U,
                                                    0x1FU,
                                                    1U,
                                                    2U,
                                                    3U,
                                                    stack_free,
                                                    frame_data,
                                                    sizeof(frame_data));

  CHECK(length == 42U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseDiagnostics(&frame, &diagnostics));
  CHECK(diagnostics.timestamp_ms == 9000U);
  CHECK(diagnostics.task_alive_bits == 0x1FU);
  CHECK(diagnostics.uart_rx_dropped == 1U);
  CHECK(diagnostics.event_queue_dropped == 2U);
  CHECK(diagnostics.watchdog_refresh_count == 3U);
  for (uint8_t index = 0U; index < BRIDGE_DIAGNOSTIC_TASK_COUNT; index++)
  {
    CHECK(diagnostics.task_stack_free[index] == stack_free[index]);
  }
  return true;
}

static bool TestCommandAndAckRoundTrip(void)
{
  uint8_t command_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  uint8_t ack_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolCommand command;
  BridgeProtocolAck ack;
  uint16_t command_length = BridgeProtocol_BuildCommand(10U,
                                                        0x1234U,
                                                        BRIDGE_CMD_PULSE_RELAY,
                                                        2U,
                                                        1500U,
                                                        0U,
                                                        command_data,
                                                        sizeof(command_data));
  uint16_t ack_length = BridgeProtocol_BuildCommandAck(11U,
                                                       0x1234U,
                                                       BRIDGE_CMD_PULSE_RELAY,
                                                       BRIDGE_RESULT_OK,
                                                       0x55AAU,
                                                       ack_data,
                                                       sizeof(ack_data));

  CHECK(command_length == 26U);
  CHECK(ack_length == 18U);
  CHECK(ParseBytes(command_data, command_length, &frame));
  CHECK(BridgeProtocol_ParseCommand(&frame, &command));
  CHECK(command.request_id == 0x1234U);
  CHECK(command.command_id == BRIDGE_CMD_PULSE_RELAY);
  CHECK(command.argument0 == 2U);
  CHECK(command.argument1 == 1500U);
  CHECK(command.argument2 == 0U);

  CHECK(ParseBytes(ack_data, ack_length, &frame));
  CHECK(BridgeProtocol_ParseCommandAck(&frame, &ack));
  CHECK(ack.request_id == 0x1234U);
  CHECK(ack.command_id == BRIDGE_CMD_PULSE_RELAY);
  CHECK(ack.result == BRIDGE_RESULT_OK);
  CHECK(ack.detail == 0x55AAU);
  return true;
}

static bool TestParserResynchronizesAfterGarbage(void)
{
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  uint8_t stream[BRIDGE_PROTOCOL_MAX_FRAME_SIZE + 4U];
  BridgeProtocolFrame frame;
  uint16_t length = BridgeProtocol_BuildHeartbeat(1U, 2U, 3U, 4U, frame_data, sizeof(frame_data));

  stream[0] = 0x00U;
  stream[1] = 0xAAU;
  stream[2] = 0x12U;
  stream[3] = 0x55U;
  memcpy(&stream[4], frame_data, length);

  CHECK(ParseBytes(stream, (uint16_t)(length + 4U), &frame));
  CHECK(frame.type == BRIDGE_MSG_HEARTBEAT);
  return true;
}

static bool TestCorruptCrcIsRejected(void)
{
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  uint16_t length = BridgeProtocol_BuildHeartbeat(1U, 2U, 3U, 4U, frame_data, sizeof(frame_data));

  frame_data[length - 1U] ^= 0xFFU;
  CHECK(!ParseBytes(frame_data, length, &frame));
  return true;
}

static bool TestUnsupportedVersionIsRejected(void)
{
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  uint16_t length = BridgeProtocol_BuildHeartbeat(1U, 2U, 3U, 4U, frame_data, sizeof(frame_data));

  frame_data[2] = 0x02U;
  CHECK(!ParseBytes(frame_data, length, &frame));
  return true;
}

static bool TestBusRxRoundTrip(void)
{
  const uint8_t rs232_payload[5] = {'H', 'e', 'l', 'l', 'o'};
  const uint8_t can_payload[8] = {0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolBusRx bus_rx;
  uint16_t length;

  length = BridgeProtocol_BuildBusRx(0x0010U,
                                     BRIDGE_BUS_RS232,
                                     0UL,
                                     rs232_payload,
                                     sizeof(rs232_payload),
                                     frame_data,
                                     sizeof(frame_data));
  CHECK(length == 21U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  CHECK(bus_rx.bus == BRIDGE_BUS_RS232);
  CHECK(bus_rx.identifier == 0U);
  CHECK(bus_rx.length == sizeof(rs232_payload));
  CHECK(memcmp(bus_rx.data, rs232_payload, sizeof(rs232_payload)) == 0);

  length = BridgeProtocol_BuildBusRx(0x0011U,
                                     BRIDGE_BUS_CAN,
                                     0x123U,
                                     can_payload,
                                     sizeof(can_payload),
                                     frame_data,
                                     sizeof(frame_data));
  CHECK(length == 24U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  CHECK(bus_rx.bus == BRIDGE_BUS_CAN);
  CHECK(bus_rx.identifier == 0x123U);
  CHECK(bus_rx.length == sizeof(can_payload));
  CHECK(memcmp(bus_rx.data, can_payload, sizeof(can_payload)) == 0);

  length = BridgeProtocol_BuildBusRx(0x0012U,
                                     BRIDGE_BUS_CAN,
                                     0x18FEF100UL,
                                     can_payload,
                                     2U,
                                     frame_data,
                                     sizeof(frame_data));
  CHECK(length > 0U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  CHECK(bus_rx.identifier == 0x18FEF100UL);
  CHECK(bus_rx.length == 2U);
  return true;
}

static bool TestBusRxRejectsInvalidInput(void)
{
  const uint8_t payload[BRIDGE_BUS_RX_MAX_DATA] = {0U};
  uint8_t frame_data[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  BridgeProtocolFrame frame;
  BridgeProtocolBusRx bus_rx;
  uint16_t length;

  CHECK(BridgeProtocol_BuildBusRx(1U, BRIDGE_BUS_RS232, 0U, payload, 0U,
                                  frame_data, sizeof(frame_data)) == 0U);
  CHECK(BridgeProtocol_BuildBusRx(1U, 0x7FU, 0U, payload, 1U,
                                  frame_data, sizeof(frame_data)) == 0U);
  CHECK(BridgeProtocol_BuildBusRx(1U, BRIDGE_BUS_CAN, 0U, payload, 9U,
                                  frame_data, sizeof(frame_data)) == 0U);
  CHECK(BridgeProtocol_BuildBusRx(1U, BRIDGE_BUS_RS232, 0U, NULL, 1U,
                                  frame_data, sizeof(frame_data)) == 0U);

  length = BridgeProtocol_BuildBusRx(1U, BRIDGE_BUS_RS232, 0U, payload,
                                     BRIDGE_BUS_RX_MAX_DATA, frame_data,
                                     sizeof(frame_data));
  CHECK(length > 0U);
  CHECK(ParseBytes(frame_data, length, &frame));
  CHECK(BridgeProtocol_ParseBusRx(&frame, &bus_rx));

  frame.type = BRIDGE_MSG_TELEMETRY;
  CHECK(!BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  frame.type = BRIDGE_MSG_BUS_RX;
  frame.payload[5] = BRIDGE_BUS_RX_MAX_DATA + 1U;
  CHECK(!BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  frame.payload[5] = 1U;
  frame.payload[0] = 0x7FU;
  CHECK(!BridgeProtocol_ParseBusRx(&frame, &bus_rx));
  CHECK(!BridgeProtocol_ParseBusRx(&frame, NULL));
  CHECK(!BridgeProtocol_ParseBusRx(NULL, &bus_rx));
  return true;
}

static bool TestShortOutputIsRejected(void)
{
  uint8_t frame_data[8];

  CHECK(BridgeProtocol_BuildHeartbeat(1U, 2U, 3U, 4U, frame_data, sizeof(frame_data)) == 0U);
  return true;
}

typedef bool (*TestFunction)(void);

typedef struct
{
  const char *name;
  TestFunction function;
} TestCase;

int main(void)
{
  int total_checks = 0;
  static const TestCase tests[] = {
      {"crc known vector", TestCrcKnownVector},
      {"heartbeat round trip", TestHeartbeatRoundTrip},
      {"telemetry round trip", TestTelemetryRoundTrip},
      {"diagnostics round trip", TestDiagnosticsRoundTrip},
      {"command and ack round trip", TestCommandAndAckRoundTrip},
      {"bus rx round trip", TestBusRxRoundTrip},
      {"bus rx rejects invalid input", TestBusRxRejectsInvalidInput},
      {"parser resynchronizes", TestParserResynchronizesAfterGarbage},
      {"corrupt crc rejected", TestCorruptCrcIsRejected},
      {"unsupported version rejected", TestUnsupportedVersionIsRejected},
      {"short output rejected", TestShortOutputIsRejected},
  };

  for (size_t index = 0U; index < (sizeof(tests) / sizeof(tests[0])); index++)
  {
    s_checks = 0;
    if (tests[index].function())
    {
      printf("PASS %s\n", tests[index].name);
    }
    else
    {
      printf("FAIL %s\n", tests[index].name);
    }
    total_checks += s_checks;
  }

  printf("%d checks, %d failures\n", total_checks, s_failures);
  return (s_failures == 0) ? 0 : 1;
}
