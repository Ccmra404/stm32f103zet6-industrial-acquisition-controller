#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "modbus_rtu.h"

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

static uint16_t AppendCrc(uint8_t *frame, uint16_t length)
{
  uint16_t crc = ModbusRtu_Crc16(frame, length);

  frame[length] = (uint8_t)(crc & 0xFFU);
  frame[length + 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
  return (uint16_t)(length + 2U);
}

static uint16_t BuildReadRequest(uint8_t *frame, uint8_t slave, uint16_t address, uint16_t quantity)
{
  frame[0] = slave;
  frame[1] = 0x03U;
  frame[2] = (uint8_t)(address >> 8U);
  frame[3] = (uint8_t)address;
  frame[4] = (uint8_t)(quantity >> 8U);
  frame[5] = (uint8_t)quantity;
  return AppendCrc(frame, 6U);
}

static uint16_t BuildWriteRequest(uint8_t *frame, uint8_t slave, uint16_t address, uint16_t value)
{
  frame[0] = slave;
  frame[1] = 0x06U;
  frame[2] = (uint8_t)(address >> 8U);
  frame[3] = (uint8_t)address;
  frame[4] = (uint8_t)(value >> 8U);
  frame[5] = (uint8_t)value;
  return AppendCrc(frame, 6U);
}

static uint16_t BuildWriteMultipleRequest(uint8_t *frame,
                                          uint8_t slave,
                                          uint16_t address,
                                          const uint16_t *values,
                                          uint16_t quantity)
{
  frame[0] = slave;
  frame[1] = 0x10U;
  frame[2] = (uint8_t)(address >> 8U);
  frame[3] = (uint8_t)address;
  frame[4] = (uint8_t)(quantity >> 8U);
  frame[5] = (uint8_t)quantity;
  frame[6] = (uint8_t)(quantity * 2U);
  for (uint16_t index = 0U; index < quantity; index++)
  {
    frame[7U + (index * 2U)] = (uint8_t)(values[index] >> 8U);
    frame[8U + (index * 2U)] = (uint8_t)values[index];
  }
  return AppendCrc(frame, (uint16_t)(7U + (quantity * 2U)));
}

static bool TestCrcKnownVector(void)
{
  const uint8_t request[] = {0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x0AU};

  CHECK(ModbusRtu_Crc16(request, sizeof(request)) == 0xCDC5U);
  return true;
}

static bool TestReadHoldingRegisters(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[32];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, 0U);
  server.registers[0] = 0x1234U;
  server.registers[1] = 0xABCDU;
  request_length = BuildReadRequest(request, 1U, 0U, 2U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 9U);
  CHECK(response[0] == 1U);
  CHECK(response[1] == 0x03U);
  CHECK(response[2] == 4U);
  CHECK(response[3] == 0x12U);
  CHECK(response[4] == 0x34U);
  CHECK(response[5] == 0xABU);
  CHECK(response[6] == 0xCDU);
  CHECK(ModbusRtu_Crc16(response, 7U) ==
        (uint16_t)((uint16_t)response[7] | ((uint16_t)response[8] << 8U)));
  return true;
}

static bool TestWriteSingleRegister(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, (UINT64_C(1) << 0x20U));
  request_length = BuildWriteRequest(request, 1U, 0x20U, 0x00A5U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == request_length);
  CHECK(memcmp(response, request, request_length) == 0);
  CHECK(server.registers[0x20U] == 0x00A5U);
  CHECK(server.write_count == 1U);
  CHECK(server.writes[0].address == 0x20U);
  CHECK(server.writes[0].value == 0x00A5U);
  return true;
}

static bool TestWriteMultipleRegisters(void)
{
  ModbusRtuServer server;
  uint8_t request[32];
  uint8_t response[16];
  uint16_t values[2] = {0x0033U, 0x0444U};
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U,
                 (UINT64_C(1) << 0x20U) | (UINT64_C(1) << 0x21U));
  request_length = BuildWriteMultipleRequest(request, 1U, 0x20U, values, 2U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 8U);
  CHECK(response[0] == 1U);
  CHECK(response[1] == 0x10U);
  CHECK(response[2] == 0x00U);
  CHECK(response[3] == 0x20U);
  CHECK(response[4] == 0x00U);
  CHECK(response[5] == 0x02U);
  CHECK(server.registers[0x20U] == values[0]);
  CHECK(server.registers[0x21U] == values[1]);
  CHECK(server.write_count == 2U);
  CHECK(server.writes[0].address == 0x20U);
  CHECK(server.writes[1].address == 0x21U);
  return true;
}

static bool TestWriteMultipleIllegalRange(void)
{
  ModbusRtuServer server;
  uint8_t request[32];
  uint8_t response[16];
  uint16_t values[2] = {1U, 2U};
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, UINT64_MAX);
  request_length = BuildWriteMultipleRequest(request, 1U, 0x3FU, values, 2U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 5U);
  CHECK(response[1] == 0x90U);
  CHECK(response[2] == MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS);
  return true;
}

static bool TestWriteMultipleByteCountMismatch(void)
{
  ModbusRtuServer server;
  uint8_t request[32];
  uint8_t response[16];
  uint16_t values[2] = {1U, 2U};
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, UINT64_MAX);
  request_length = BuildWriteMultipleRequest(request, 1U, 0x20U, values, 2U);
  request[6] = 2U;
  request_length = AppendCrc(request, (uint16_t)(7U + request[6]));
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 5U);
  CHECK(response[1] == 0x90U);
  CHECK(response[2] == MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE);
  return true;
}

static bool TestIllegalAddressException(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, 0U);
  request_length = BuildWriteRequest(request, 1U, 0x00U, 1U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 5U);
  CHECK(response[0] == 1U);
  CHECK(response[1] == 0x86U);
  CHECK(response[2] == MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS);
  return true;
}

static bool TestIllegalFunctionException(void)
{
  ModbusRtuServer server;
  uint8_t request[16] = {1U, 0x04U, 0U, 0U, 0U, 1U};
  uint8_t response[16];
  uint16_t request_length = AppendCrc(request, 6U);
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, 0U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 5U);
  CHECK(response[1] == 0x84U);
  CHECK(response[2] == MODBUS_RTU_EXCEPTION_ILLEGAL_FUNCTION);
  return true;
}

static bool TestIllegalQuantityException(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, 0U);
  request_length = BuildReadRequest(request, 1U, 0U, 0U);
  response_length = ModbusRtu_Process(&server, request, request_length, response, sizeof(response));

  CHECK(response_length == 5U);
  CHECK(response[1] == 0x83U);
  CHECK(response[2] == MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE);
  return true;
}

static bool TestBadCrcIsIgnored(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;

  ModbusRtu_Init(&server, 1U, 0U);
  request_length = BuildReadRequest(request, 1U, 0U, 1U);
  request[request_length - 1U] ^= 0xFFU;

  CHECK(ModbusRtu_Process(&server, request, request_length, response, sizeof(response)) == 0U);
  return true;
}

static bool TestWrongSlaveIsIgnored(void)
{
  ModbusRtuServer server;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;

  ModbusRtu_Init(&server, 1U, 0U);
  request_length = BuildReadRequest(request, 2U, 0U, 1U);

  CHECK(ModbusRtu_Process(&server, request, request_length, response, sizeof(response)) == 0U);
  return true;
}

static bool TestMasterBuildReadRequest(void)
{
  uint8_t request[16];
  uint16_t request_length;

  request_length = ModbusRtu_BuildReadRequest(1U,
                                              MODBUS_RTU_FUNCTION_READ_HOLDING,
                                              0U,
                                              2U,
                                              request,
                                              sizeof(request));

  CHECK(request_length == 8U);
  CHECK(request[0] == 1U);
  CHECK(request[1] == 0x03U);
  CHECK(request[2] == 0U);
  CHECK(request[3] == 0U);
  CHECK(request[4] == 0U);
  CHECK(request[5] == 2U);
  CHECK(request[6] == 0xC4U);
  CHECK(request[7] == 0x0BU);

  CHECK(ModbusRtu_BuildReadRequest(1U, 0x05U, 0U, 1U, request, sizeof(request)) == 0U);
  CHECK(ModbusRtu_BuildReadRequest(1U,
                                   MODBUS_RTU_FUNCTION_READ_HOLDING,
                                   0U,
                                   0U,
                                   request,
                                   sizeof(request)) == 0U);
  CHECK(ModbusRtu_BuildReadRequest(1U,
                                   MODBUS_RTU_FUNCTION_READ_HOLDING,
                                   0U,
                                   126U,
                                   request,
                                   sizeof(request)) == 0U);
  CHECK(ModbusRtu_BuildReadRequest(1U,
                                   MODBUS_RTU_FUNCTION_READ_HOLDING,
                                   0U,
                                   1U,
                                   request,
                                   7U) == 0U);
  return true;
}

static bool TestMasterReadRoundTrip(void)
{
  ModbusRtuServer server;
  ModbusRtuResponse parsed;
  uint8_t request[16];
  uint8_t response[32];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 7U, 0U);
  server.registers[3] = 0x1234U;
  server.registers[4] = 0xABCDU;

  request_length = ModbusRtu_BuildReadRequest(7U,
                                              MODBUS_RTU_FUNCTION_READ_HOLDING,
                                              3U,
                                              2U,
                                              request,
                                              sizeof(request));
  response_length = ModbusRtu_Process(&server,
                                      request,
                                      request_length,
                                      response,
                                      sizeof(response));

  CHECK(response_length == 9U);
  CHECK(ModbusRtu_ParseResponse(7U, response, response_length, &parsed) == true);
  CHECK(parsed.exception_code == 0U);
  CHECK(parsed.function == MODBUS_RTU_FUNCTION_READ_HOLDING);
  CHECK(parsed.register_count == 2U);
  CHECK(parsed.quantity == 2U);
  CHECK(parsed.registers[0] == 0x1234U);
  CHECK(parsed.registers[1] == 0xABCDU);
  return true;
}

static bool TestMasterWriteSingleRoundTrip(void)
{
  ModbusRtuServer server;
  ModbusRtuResponse parsed;
  uint8_t request[16];
  uint8_t response[16];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server, 1U, UINT64_C(1) << 0x20U);
  request_length = ModbusRtu_BuildWriteSingleRequest(1U,
                                                     0x20U,
                                                     0x00A5U,
                                                     request,
                                                     sizeof(request));
  response_length = ModbusRtu_Process(&server,
                                      request,
                                      request_length,
                                      response,
                                      sizeof(response));

  CHECK(response_length == 8U);
  CHECK(server.registers[0x20U] == 0x00A5U);
  CHECK(server.write_count == 1U);
  CHECK(server.writes[0].address == 0x20U);
  CHECK(server.writes[0].value == 0x00A5U);
  CHECK(ModbusRtu_ParseResponse(1U, response, response_length, &parsed) == true);
  CHECK(parsed.address == 0x20U);
  CHECK(parsed.registers[0] == 0x00A5U);
  CHECK(parsed.quantity == 1U);
  return true;
}

static bool TestMasterWriteMultipleRoundTrip(void)
{
  ModbusRtuServer server;
  ModbusRtuResponse parsed;
  const uint16_t values[3] = {0x1111U, 0x2222U, 0x3333U};
  uint8_t request[32];
  uint8_t response[16];
  uint16_t request_length;
  uint16_t response_length;

  ModbusRtu_Init(&server,
                 2U,
                 (UINT64_C(1) << 0x21U) | (UINT64_C(1) << 0x22U) |
                     (UINT64_C(1) << 0x23U));
  request_length = ModbusRtu_BuildWriteMultipleRequest(2U,
                                                       0x21U,
                                                       values,
                                                       3U,
                                                       request,
                                                       sizeof(request));

  CHECK(request_length == 15U);
  response_length = ModbusRtu_Process(&server,
                                      request,
                                      request_length,
                                      response,
                                      sizeof(response));

  CHECK(response_length == 8U);
  CHECK(server.registers[0x21U] == 0x1111U);
  CHECK(server.registers[0x22U] == 0x2222U);
  CHECK(server.registers[0x23U] == 0x3333U);
  CHECK(ModbusRtu_ParseResponse(2U, response, response_length, &parsed) == true);
  CHECK(parsed.address == 0x21U);
  CHECK(parsed.quantity == 3U);
  return true;
}

static bool TestMasterParsesException(void)
{
  ModbusRtuResponse parsed;
  uint8_t response[8];
  uint16_t response_length;

  response[0] = 3U;
  response[1] = (uint8_t)(MODBUS_RTU_FUNCTION_READ_HOLDING | 0x80U);
  response[2] = MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS;
  response_length = AppendCrc(response, 3U);

  CHECK(ModbusRtu_ParseResponse(3U, response, response_length, &parsed) == true);
  CHECK(parsed.exception_code == MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS);
  CHECK(parsed.function == MODBUS_RTU_FUNCTION_READ_HOLDING);
  CHECK(parsed.register_count == 0U);
  return true;
}

static bool TestMasterRejectsMalformedResponse(void)
{
  ModbusRtuResponse parsed;
  uint8_t response[16];
  uint16_t response_length;

  response[0] = 1U;
  response[1] = MODBUS_RTU_FUNCTION_READ_HOLDING;
  response[2] = 4U;
  response[3] = 0x12U;
  response[4] = 0x34U;
  response[5] = 0xABU;
  response[6] = 0xCDU;
  response_length = AppendCrc(response, 7U);

  CHECK(ModbusRtu_ParseResponse(2U, response, response_length, &parsed) == false);

  response[response_length - 1U] ^= 0xFFU;
  CHECK(ModbusRtu_ParseResponse(1U, response, response_length, &parsed) == false);

  response_length = AppendCrc(response, 7U);
  response[2] = 3U;
  response[7] = 0U;
  response[8] = 0U;
  response_length = AppendCrc(response, 9U);
  CHECK(ModbusRtu_ParseResponse(1U, response, response_length, &parsed) == false);

  CHECK(ModbusRtu_ParseResponse(1U, response, 4U, &parsed) == false);
  CHECK(ModbusRtu_ParseResponse(1U, NULL, 8U, &parsed) == false);
  CHECK(ModbusRtu_ParseResponse(1U, response, response_length, NULL) == false);
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
      {"modbus crc known vector", TestCrcKnownVector},
      {"read holding registers", TestReadHoldingRegisters},
      {"write single register", TestWriteSingleRegister},
      {"write multiple registers", TestWriteMultipleRegisters},
      {"write multiple illegal range", TestWriteMultipleIllegalRange},
      {"write multiple byte count mismatch", TestWriteMultipleByteCountMismatch},
      {"illegal address exception", TestIllegalAddressException},
      {"illegal function exception", TestIllegalFunctionException},
      {"illegal quantity exception", TestIllegalQuantityException},
      {"bad crc ignored", TestBadCrcIsIgnored},
      {"wrong slave ignored", TestWrongSlaveIsIgnored},
      {"master build read request", TestMasterBuildReadRequest},
      {"master read round trip", TestMasterReadRoundTrip},
      {"master write single round trip", TestMasterWriteSingleRoundTrip},
      {"master write multiple round trip", TestMasterWriteMultipleRoundTrip},
      {"master parses exception", TestMasterParsesException},
      {"master rejects malformed response", TestMasterRejectsMalformedResponse},
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
