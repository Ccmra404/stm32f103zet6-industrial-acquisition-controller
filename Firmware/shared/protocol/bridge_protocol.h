#ifndef BRIDGE_PROTOCOL_H
#define BRIDGE_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_PROTOCOL_VERSION 0x01U
#define BRIDGE_PROTOCOL_SOF0 0xAAU
#define BRIDGE_PROTOCOL_SOF1 0x55U
#define BRIDGE_PROTOCOL_MAX_PAYLOAD 256U
#define BRIDGE_PROTOCOL_MAX_FRAME_SIZE (10U + BRIDGE_PROTOCOL_MAX_PAYLOAD)

#define BRIDGE_MSG_HELLO 0x01U
#define BRIDGE_MSG_HEARTBEAT 0x02U
#define BRIDGE_MSG_TELEMETRY 0x10U
#define BRIDGE_MSG_EVENT 0x11U
#define BRIDGE_MSG_COMMAND 0x20U
#define BRIDGE_MSG_COMMAND_ACK 0x21U

#define BRIDGE_CMD_SET_RELAY_MASK 0x0001U
#define BRIDGE_CMD_PULSE_RELAY 0x0002U
#define BRIDGE_CMD_SET_ANALOG_OUTPUT 0x0003U
#define BRIDGE_CMD_CLEAR_FAULTS 0x0004U
#define BRIDGE_CMD_SAVE_CONFIG 0x0200U
#define BRIDGE_CMD_LOAD_CONFIG 0x0201U

#define BRIDGE_RESULT_OK 0
#define BRIDGE_RESULT_UNSUPPORTED 1
#define BRIDGE_RESULT_INVALID_ARGUMENT 2
#define BRIDGE_RESULT_BUSY 3
#define BRIDGE_RESULT_NOT_READY 4
#define BRIDGE_RESULT_OUT_OF_RANGE 5
#define BRIDGE_RESULT_STORAGE_ERROR 6
#define BRIDGE_RESULT_SAFETY_LOCK 7

#define BRIDGE_ROLE_STM32 1U
#define BRIDGE_CAP_ANALOG_INPUT (1UL << 0)
#define BRIDGE_CAP_TEMPERATURE (1UL << 1)
#define BRIDGE_CAP_DIGITAL_INPUT (1UL << 2)
#define BRIDGE_CAP_RELAY_OUTPUT (1UL << 3)
#define BRIDGE_CAP_ANALOG_OUTPUT (1UL << 4)
#define BRIDGE_CAP_RS485 (1UL << 5)
#define BRIDGE_CAP_RS232 (1UL << 6)
#define BRIDGE_CAP_CAN (1UL << 7)
#define BRIDGE_CAP_EEPROM (1UL << 8)
#define BRIDGE_CAP_LCD (1UL << 9)
#define BRIDGE_CAP_AUDIO (1UL << 10)
#define BRIDGE_CAP_WIFI (1UL << 11)

#define BRIDGE_EVENT_DIGITAL_INPUT 0x0001U
#define BRIDGE_EVENT_RELAY_CHANGE 0x0002U

typedef struct
{
  uint8_t version;
  uint8_t type;
  uint16_t sequence;
  uint16_t length;
  uint8_t payload[BRIDGE_PROTOCOL_MAX_PAYLOAD];
} BridgeProtocolFrame;

typedef struct
{
  uint8_t buffer[BRIDGE_PROTOCOL_MAX_FRAME_SIZE];
  uint16_t count;
  uint16_t expected;
} BridgeProtocolParser;

typedef struct
{
  uint16_t request_id;
  uint16_t command_id;
  uint32_t argument0;
  uint32_t argument1;
  uint32_t argument2;
} BridgeProtocolCommand;

typedef struct
{
  uint8_t role;
  uint16_t firmware_version;
  uint32_t capabilities;
} BridgeProtocolHello;

typedef struct
{
  uint32_t uptime_ms;
  uint16_t health_flags;
  uint16_t fault_bits;
} BridgeProtocolHeartbeat;

typedef struct
{
  uint32_t timestamp_ms;
  int32_t ai_raw[8];
  int32_t rtd_millicelsius;
  uint8_t di_bits;
  uint8_t relay_bits;
  uint16_t supply_mv[2];
  uint16_t fault_bits;
} BridgeProtocolTelemetry;

typedef struct
{
  uint16_t request_id;
  uint16_t command_id;
  int16_t result;
  uint16_t detail;
} BridgeProtocolAck;

uint16_t BridgeProtocol_Crc16(const uint8_t *data, uint16_t length);
void BridgeProtocol_ParserInit(BridgeProtocolParser *parser);
bool BridgeProtocol_ParserPushByte(BridgeProtocolParser *parser,
                                   uint8_t byte,
                                   BridgeProtocolFrame *frame);

uint16_t BridgeProtocol_BuildHeartbeat(uint16_t sequence,
                                       uint32_t uptime_ms,
                                       uint16_t health_flags,
                                       uint16_t fault_bits,
                                       uint8_t *output,
                                       uint16_t output_size);

uint16_t BridgeProtocol_BuildHello(uint16_t sequence,
                                   uint8_t role,
                                   uint16_t firmware_version,
                                   uint32_t capabilities,
                                   uint8_t *output,
                                   uint16_t output_size);

uint16_t BridgeProtocol_BuildEvent(uint16_t sequence,
                                   uint32_t timestamp_ms,
                                   uint16_t event_code,
                                   uint32_t argument0,
                                   uint32_t argument1,
                                   uint8_t *output,
                                   uint16_t output_size);

uint16_t BridgeProtocol_BuildTelemetry(uint16_t sequence,
                                       uint32_t timestamp_ms,
                                       const int32_t ai_raw[8],
                                       int32_t rtd_millicelsius,
                                       uint8_t di_bits,
                                       uint8_t relay_bits,
                                       const uint16_t supply_mv[2],
                                       uint16_t fault_bits,
                                       uint8_t *output,
                                       uint16_t output_size);

uint16_t BridgeProtocol_BuildCommandAck(uint16_t sequence,
                                        uint16_t request_id,
                                        uint16_t command_id,
                                        int16_t result,
                                        uint16_t detail,
                                        uint8_t *output,
                                        uint16_t output_size);

uint16_t BridgeProtocol_BuildCommand(uint16_t sequence,
                                     uint16_t request_id,
                                     uint16_t command_id,
                                     uint32_t argument0,
                                     uint32_t argument1,
                                     uint32_t argument2,
                                     uint8_t *output,
                                     uint16_t output_size);

bool BridgeProtocol_ParseCommand(const BridgeProtocolFrame *frame,
                                 BridgeProtocolCommand *command);
bool BridgeProtocol_ParseHello(const BridgeProtocolFrame *frame,
                               BridgeProtocolHello *hello);
bool BridgeProtocol_ParseHeartbeat(const BridgeProtocolFrame *frame,
                                   BridgeProtocolHeartbeat *heartbeat);
bool BridgeProtocol_ParseTelemetry(const BridgeProtocolFrame *frame,
                                   BridgeProtocolTelemetry *telemetry);
bool BridgeProtocol_ParseCommandAck(const BridgeProtocolFrame *frame,
                                    BridgeProtocolAck *ack);

#ifdef __cplusplus
}
#endif

#endif
