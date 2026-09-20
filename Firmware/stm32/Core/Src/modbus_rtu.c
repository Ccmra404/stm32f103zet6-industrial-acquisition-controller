#include "modbus_rtu.h"

#include <string.h>

static uint16_t ReadU16Be(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

static void WriteU16Be(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)((value >> 8U) & 0xFFU);
  output[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t AppendCrc(uint8_t *response, uint16_t length)
{
  uint16_t crc = ModbusRtu_Crc16(response, length);

  response[length] = (uint8_t)(crc & 0xFFU);
  response[length + 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
  return (uint16_t)(length + 2U);
}

static uint16_t BuildException(uint8_t slave_address,
                               uint8_t function,
                               uint8_t exception,
                               uint8_t *response,
                               uint16_t response_size)
{
  if (response_size < 5U)
  {
    return 0U;
  }

  response[0] = slave_address;
  response[1] = (uint8_t)(function | 0x80U);
  response[2] = exception;
  return AppendCrc(response, 3U);
}

uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;

  if (data == 0)
  {
    return crc;
  }

  for (uint16_t index = 0U; index < length; index++)
  {
    crc ^= data[index];
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xA001U) : (uint16_t)(crc >> 1U);
    }
  }

  return crc;
}

void ModbusRtu_Init(ModbusRtuServer *server, uint8_t slave_address, uint64_t writable_mask)
{
  if (server == 0)
  {
    return;
  }

  memset(server, 0, sizeof(*server));
  server->slave_address = slave_address;
  server->writable_mask = writable_mask;
}

uint16_t ModbusRtu_Process(ModbusRtuServer *server,
                           const uint8_t *request,
                           uint16_t request_length,
                           uint8_t *response,
                           uint16_t response_size)
{
  uint16_t received_crc;
  uint16_t calculated_crc;
  uint8_t function;
  uint16_t address;
  uint16_t quantity;

  if ((server == 0) || (request == 0) || (response == 0) ||
      (request_length < 4U) || (response_size < 5U))
  {
    return 0U;
  }

  if (request[0] != server->slave_address)
  {
    return 0U;
  }

  received_crc = (uint16_t)((uint16_t)request[request_length - 2U] |
                            ((uint16_t)request[request_length - 1U] << 8U));
  calculated_crc = ModbusRtu_Crc16(request, (uint16_t)(request_length - 2U));
  if (received_crc != calculated_crc)
  {
    return 0U;
  }

  function = request[1];
  server->write_count = 0U;

  if (function == 0x03U)
  {
    uint16_t byte_count;

    if (request_length != 8U)
    {
      return 0U;
    }

    address = ReadU16Be(&request[2]);
    quantity = ReadU16Be(&request[4]);
    if ((quantity == 0U) || (quantity > 125U))
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE,
                            response, response_size);
    }

    if (((uint32_t)address + quantity) > MODBUS_RTU_MAX_REGISTERS)
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS,
                            response, response_size);
    }

    byte_count = (uint16_t)(quantity * 2U);
    if (response_size < (uint16_t)(byte_count + 5U))
    {
      return 0U;
    }

    response[0] = server->slave_address;
    response[1] = function;
    response[2] = (uint8_t)byte_count;
    for (uint16_t index = 0U; index < quantity; index++)
    {
      WriteU16Be(&response[3U + (index * 2U)], server->registers[address + index]);
    }
    return AppendCrc(response, (uint16_t)(byte_count + 3U));
  }

  if (function == 0x06U)
  {
    if (request_length != 8U)
    {
      return 0U;
    }

    address = ReadU16Be(&request[2]);
    if (address >= MODBUS_RTU_MAX_REGISTERS)
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS,
                            response, response_size);
    }

    if ((server->writable_mask & (UINT64_C(1) << address)) == 0U)
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS,
                            response, response_size);
    }

    server->registers[address] = ReadU16Be(&request[4]);
    if (server->write_count < MODBUS_RTU_MAX_WRITES)
    {
      server->writes[server->write_count].address = address;
      server->writes[server->write_count].value = server->registers[address];
      server->write_count++;
    }

    if (response_size < request_length)
    {
      return 0U;
    }
    memcpy(response, request, request_length);
    return request_length;
  }

  if (function == 0x10U)
  {
    uint8_t byte_count;

    if (request_length < 11U)
    {
      return 0U;
    }

    address = ReadU16Be(&request[2]);
    quantity = ReadU16Be(&request[4]);
    byte_count = request[6];
    if ((quantity == 0U) || (quantity > 123U) ||
        (byte_count != (uint8_t)(quantity * 2U)) ||
        (request_length != (uint16_t)(9U + byte_count)))
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE,
                            response, response_size);
    }

    if (((uint32_t)address + quantity) > MODBUS_RTU_MAX_REGISTERS)
    {
      return BuildException(server->slave_address, function,
                            MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS,
                            response, response_size);
    }

    for (uint16_t index = 0U; index < quantity; index++)
    {
      if ((server->writable_mask & (UINT64_C(1) << (address + index))) == 0U)
      {
        return BuildException(server->slave_address, function,
                              MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS,
                              response, response_size);
      }
    }

    for (uint16_t index = 0U; index < quantity; index++)
    {
      server->registers[address + index] = ReadU16Be(&request[7U + (index * 2U)]);
      if (server->write_count < MODBUS_RTU_MAX_WRITES)
      {
        server->writes[server->write_count].address = (uint16_t)(address + index);
        server->writes[server->write_count].value = server->registers[address + index];
        server->write_count++;
      }
    }

    if (response_size < 8U)
    {
      return 0U;
    }

    response[0] = server->slave_address;
    response[1] = function;
    WriteU16Be(&response[2], address);
    WriteU16Be(&response[4], quantity);
    return AppendCrc(response, 6U);
  }

  return BuildException(server->slave_address, function,
                        MODBUS_RTU_EXCEPTION_ILLEGAL_FUNCTION,
                        response, response_size);
}
