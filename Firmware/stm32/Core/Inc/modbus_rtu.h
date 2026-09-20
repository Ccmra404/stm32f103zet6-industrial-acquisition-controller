#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_RTU_MAX_REGISTERS 64U
#define MODBUS_RTU_MAX_WRITES 128U

#define MODBUS_RTU_FUNCTION_READ_HOLDING 0x03U
#define MODBUS_RTU_FUNCTION_READ_INPUT 0x04U
#define MODBUS_RTU_FUNCTION_WRITE_SINGLE 0x06U
#define MODBUS_RTU_FUNCTION_WRITE_MULTIPLE 0x10U

#define MODBUS_RTU_EXCEPTION_ILLEGAL_FUNCTION 0x01U
#define MODBUS_RTU_EXCEPTION_ILLEGAL_ADDRESS 0x02U
#define MODBUS_RTU_EXCEPTION_ILLEGAL_VALUE 0x03U

typedef struct
{
  uint16_t address;
  uint16_t value;
} ModbusRtuWrite;

/*
 * Parsed master-side response. Registers are only filled for read
 * responses; exception_code is non-zero when the peer answered with a
 * Modbus exception instead of data.
 */
typedef struct
{
  uint8_t slave_address;
  uint8_t function;
  uint8_t exception_code;
  uint16_t address;
  uint16_t quantity;
  uint16_t registers[MODBUS_RTU_MAX_REGISTERS];
  uint16_t register_count;
} ModbusRtuResponse;

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

/* Master-side request builders: return frame length, 0 on invalid input. */
uint16_t ModbusRtu_BuildReadRequest(uint8_t slave_address,
                                    uint8_t function,
                                    uint16_t address,
                                    uint16_t quantity,
                                    uint8_t *request,
                                    uint16_t request_size);

uint16_t ModbusRtu_BuildWriteSingleRequest(uint8_t slave_address,
                                           uint16_t address,
                                           uint16_t value,
                                           uint8_t *request,
                                           uint16_t request_size);

uint16_t ModbusRtu_BuildWriteMultipleRequest(uint8_t slave_address,
                                             uint16_t address,
                                             const uint16_t *values,
                                             uint16_t quantity,
                                             uint8_t *request,
                                             uint16_t request_size);

/*
 * Validates slave address, CRC and frame layout. Returns false for
 * malformed frames; a well-formed exception response returns true with
 * exception_code set.
 */
bool ModbusRtu_ParseResponse(uint8_t slave_address,
                             const uint8_t *response,
                             uint16_t response_length,
                             ModbusRtuResponse *parsed);

#ifdef __cplusplus
}
#endif

#endif
