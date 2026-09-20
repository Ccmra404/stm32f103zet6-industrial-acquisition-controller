#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MODBUS_RTU_MAX_REGISTERS 64U
#define MODBUS_RTU_MAX_WRITES 8U

#define MODBUS_RTU_EXCEPTION_ILLEGAL_FUNCTION 0x01U
#define MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS 0x02U
#define MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE 0x03U

typedef struct
{
  uint16_t address;
  uint16_t value;
} ModbusRtuWrite;

typedef struct
{
  uint8_t slave_address;
  uint16_t registers[MODBUS_RTU_MAX_REGISTERS];
  uint64_t writable_mask;
  ModbusRtuWrite writes[MODBUS_RTU_MAX_WRITES];
  uint8_t write_count;
} ModbusRtuServer;

uint16_t ModbusRtu_Crc16(const uint8_t *data, uint16_t length);
void ModbusRtu_Init(ModbusRtuServer *server, uint8_t slave_address, uint64_t writable_mask);
uint16_t ModbusRtu_Process(ModbusRtuServer *server,
                           const uint8_t *request,
                           uint16_t request_length,
                           uint8_t *response,
                           uint16_t response_size);

#ifdef __cplusplus
}
#endif

#endif
