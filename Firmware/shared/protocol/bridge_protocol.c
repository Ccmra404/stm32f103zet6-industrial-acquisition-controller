#include "bridge_protocol.h"

#include <string.h>

#define FRAME_HEADER_SIZE 8U
#define FRAME_CRC_SIZE 2U

static void WriteU16Le(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value & 0xFFU);
  output[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void WriteU32Le(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value & 0xFFU);
  output[1] = (uint8_t)((value >> 8U) & 0xFFU);
  output[2] = (uint8_t)((value >> 16U) & 0xFFU);
  output[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static uint16_t ReadU16Le(const uint8_t *input)
{
  return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static uint32_t ReadU32Le(const uint8_t *input)
{
  return ((uint32_t)input[0]) |
         ((uint32_t)input[1] << 8U) |
         ((uint32_t)input[2] << 16U) |
         ((uint32_t)input[3] << 24U);
}

static uint16_t BuildFrame(uint8_t type,
                           uint16_t sequence,
                           const uint8_t *payload,
                           uint16_t payload_length,
                           uint8_t *output,
                           uint16_t output_size)
{
  uint16_t frame_length;
  uint16_t crc;

  if ((payload == 0) ||
      (payload_length > BRIDGE_PROTOCOL_MAX_PAYLOAD) ||
      (output_size < (uint16_t)(FRAME_HEADER_SIZE + payload_length + FRAME_CRC_SIZE)))
  {
    return 0U;
  }

  output[0] = BRIDGE_PROTOCOL_SOF0;
  output[1] = BRIDGE_PROTOCOL_SOF1;
  output[2] = BRIDGE_PROTOCOL_VERSION;
  output[3] = type;
  WriteU16Le(&output[4], sequence);
  WriteU16Le(&output[6], payload_length);
  memcpy(&output[FRAME_HEADER_SIZE], payload, payload_length);

  frame_length = (uint16_t)(FRAME_HEADER_SIZE + payload_length);
  crc = BridgeProtocol_Crc16(&output[2], (uint16_t)(frame_length - 2U));
  WriteU16Le(&output[frame_length], crc);

  return (uint16_t)(frame_length + FRAME_CRC_SIZE);
}

uint16_t BridgeProtocol_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;

  for (index = 0U; index < length; index++)
  {
    uint8_t bit;

    crc ^= (uint16_t)data[index] << 8U;
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U)
                            : (uint16_t)(crc << 1U);
    }
  }

  return crc;
}

void BridgeProtocol_ParserInit(BridgeProtocolParser *parser)
{
  if (parser != 0)
  {
    parser->count = 0U;
    parser->expected = 0U;
  }
}

bool BridgeProtocol_ParserPushByte(BridgeProtocolParser *parser,
                                   uint8_t byte,
                                   BridgeProtocolFrame *frame)
{
  uint16_t expected;
  uint16_t received_crc;
  uint16_t calculated_crc;

  if ((parser == 0) || (frame == 0))
  {
    return false;
  }

  if (parser->count == 0U)
  {
    if (byte == BRIDGE_PROTOCOL_SOF0)
    {
      parser->buffer[parser->count++] = byte;
    }
    return false;
  }

  if (parser->count == 1U)
  {
    if (byte == BRIDGE_PROTOCOL_SOF1)
    {
      parser->buffer[parser->count++] = byte;
    }
    else
    {
      parser->count = (byte == BRIDGE_PROTOCOL_SOF0) ? 1U : 0U;
      if (parser->count == 1U)
      {
        parser->buffer[0] = BRIDGE_PROTOCOL_SOF0;
      }
    }
    return false;
  }

  if (parser->count < FRAME_HEADER_SIZE)
  {
    parser->buffer[parser->count++] = byte;
    if (parser->count == FRAME_HEADER_SIZE)
    {
      uint16_t payload_length = ReadU16Le(&parser->buffer[6]);

      if ((parser->buffer[2] != BRIDGE_PROTOCOL_VERSION) ||
          (payload_length > BRIDGE_PROTOCOL_MAX_PAYLOAD))
      {
        parser->count = 0U;
        return false;
      }

      parser->expected = (uint16_t)(FRAME_HEADER_SIZE + payload_length + FRAME_CRC_SIZE);
    }
    return false;
  }

  if (parser->count < parser->expected)
  {
    parser->buffer[parser->count++] = byte;
  }

  if ((parser->expected == 0U) || (parser->count < parser->expected))
  {
    return false;
  }

  expected = parser->expected;
  received_crc = ReadU16Le(&parser->buffer[expected - FRAME_CRC_SIZE]);
  calculated_crc = BridgeProtocol_Crc16(&parser->buffer[2], (uint16_t)(expected - 4U));

  parser->count = 0U;
  parser->expected = 0U;

  if (received_crc != calculated_crc)
  {
    return false;
  }

  frame->version = parser->buffer[2];
  frame->type = parser->buffer[3];
  frame->sequence = ReadU16Le(&parser->buffer[4]);
  frame->length = ReadU16Le(&parser->buffer[6]);
  memcpy(frame->payload, &parser->buffer[FRAME_HEADER_SIZE], frame->length);
  return true;
}

uint16_t BridgeProtocol_BuildHeartbeat(uint16_t sequence,
                                       uint32_t uptime_ms,
                                       uint16_t health_flags,
                                       uint16_t fault_bits,
                                       uint8_t *output,
                                       uint16_t output_size)
{
  uint8_t payload[8];

  WriteU32Le(&payload[0], uptime_ms);
  WriteU16Le(&payload[4], health_flags);
  WriteU16Le(&payload[6], fault_bits);
  return BuildFrame(BRIDGE_MSG_HEARTBEAT, sequence, payload, sizeof(payload), output, output_size);
}

uint16_t BridgeProtocol_BuildHello(uint16_t sequence,
                                   uint8_t role,
                                   uint16_t firmware_version,
                                   uint32_t capabilities,
                                   uint8_t *output,
                                   uint16_t output_size)
{
  uint8_t payload[7];

  payload[0] = role;
  WriteU16Le(&payload[1], firmware_version);
  WriteU32Le(&payload[3], capabilities);
  return BuildFrame(BRIDGE_MSG_HELLO, sequence, payload, sizeof(payload), output, output_size);
}

uint16_t BridgeProtocol_BuildEvent(uint16_t sequence,
                                   uint32_t timestamp_ms,
                                   uint16_t event_code,
                                   uint32_t argument0,
                                   uint32_t argument1,
                                   uint8_t *output,
                                   uint16_t output_size)
{
  uint8_t payload[14];

  WriteU32Le(&payload[0], timestamp_ms);
  WriteU16Le(&payload[4], event_code);
  WriteU32Le(&payload[6], argument0);
  WriteU32Le(&payload[10], argument1);
  return BuildFrame(BRIDGE_MSG_EVENT, sequence, payload, sizeof(payload), output, output_size);
}

uint16_t BridgeProtocol_BuildTelemetry(uint16_t sequence,
                                       uint32_t timestamp_ms,
                                       const int32_t ai_raw[8],
                                       int32_t rtd_millicelsius,
                                       uint8_t di_bits,
                                       uint8_t relay_bits,
                                       const uint16_t supply_mv[2],
                                       uint16_t fault_bits,
                                       uint8_t *output,
                                       uint16_t output_size)
{
  uint8_t payload[48];
  uint16_t offset = 0U;
  uint8_t index;

  if ((ai_raw == 0) || (supply_mv == 0))
  {
    return 0U;
  }

  WriteU32Le(&payload[offset], timestamp_ms);
  offset += 4U;
  for (index = 0U; index < 8U; index++)
  {
    WriteU32Le(&payload[offset], (uint32_t)ai_raw[index]);
    offset += 4U;
  }
  WriteU32Le(&payload[offset], (uint32_t)rtd_millicelsius);
  offset += 4U;
  payload[offset++] = di_bits;
  payload[offset++] = relay_bits;
  WriteU16Le(&payload[offset], supply_mv[0]);
  offset += 2U;
  WriteU16Le(&payload[offset], supply_mv[1]);
  offset += 2U;
  WriteU16Le(&payload[offset], fault_bits);

  return BuildFrame(BRIDGE_MSG_TELEMETRY, sequence, payload, sizeof(payload), output, output_size);
}

uint16_t BridgeProtocol_BuildCommandAck(uint16_t sequence,
                                        uint16_t request_id,
                                        uint16_t command_id,
                                        int16_t result,
                                        uint16_t detail,
                                        uint8_t *output,
                                        uint16_t output_size)
{
  uint8_t payload[8];

  WriteU16Le(&payload[0], request_id);
  WriteU16Le(&payload[2], command_id);
  WriteU16Le(&payload[4], (uint16_t)result);
  WriteU16Le(&payload[6], detail);
  return BuildFrame(BRIDGE_MSG_COMMAND_ACK, sequence, payload, sizeof(payload), output, output_size);
}

uint16_t BridgeProtocol_BuildCommand(uint16_t sequence,
                                     uint16_t request_id,
                                     uint16_t command_id,
                                     uint32_t argument0,
                                     uint32_t argument1,
                                     uint32_t argument2,
                                     uint8_t *output,
                                     uint16_t output_size)
{
  uint8_t payload[16];

  WriteU16Le(&payload[0], request_id);
  WriteU16Le(&payload[2], command_id);
  WriteU32Le(&payload[4], argument0);
  WriteU32Le(&payload[8], argument1);
  WriteU32Le(&payload[12], argument2);
  return BuildFrame(BRIDGE_MSG_COMMAND, sequence, payload, sizeof(payload), output, output_size);
}

bool BridgeProtocol_ParseCommand(const BridgeProtocolFrame *frame,
                                 BridgeProtocolCommand *command)
{
  if ((frame == 0) || (command == 0) ||
      (frame->type != BRIDGE_MSG_COMMAND) ||
      (frame->length != 16U))
  {
    return false;
  }

  command->request_id = ReadU16Le(&frame->payload[0]);
  command->command_id = ReadU16Le(&frame->payload[2]);
  command->argument0 = ReadU32Le(&frame->payload[4]);
  command->argument1 = ReadU32Le(&frame->payload[8]);
  command->argument2 = ReadU32Le(&frame->payload[12]);
  return true;
}

bool BridgeProtocol_ParseHello(const BridgeProtocolFrame *frame,
                               BridgeProtocolHello *hello)
{
  if ((frame == 0) || (hello == 0) ||
      (frame->type != BRIDGE_MSG_HELLO) ||
      (frame->length != 7U))
  {
    return false;
  }

  hello->role = frame->payload[0];
  hello->firmware_version = ReadU16Le(&frame->payload[1]);
  hello->capabilities = ReadU32Le(&frame->payload[3]);
  return true;
}

bool BridgeProtocol_ParseHeartbeat(const BridgeProtocolFrame *frame,
                                   BridgeProtocolHeartbeat *heartbeat)
{
  if ((frame == 0) || (heartbeat == 0) ||
      (frame->type != BRIDGE_MSG_HEARTBEAT) ||
      (frame->length != 8U))
  {
    return false;
  }

  heartbeat->uptime_ms = ReadU32Le(&frame->payload[0]);
  heartbeat->health_flags = ReadU16Le(&frame->payload[4]);
  heartbeat->fault_bits = ReadU16Le(&frame->payload[6]);
  return true;
}

bool BridgeProtocol_ParseTelemetry(const BridgeProtocolFrame *frame,
                                   BridgeProtocolTelemetry *telemetry)
{
  uint16_t offset = 0U;
  uint8_t index;

  if ((frame == 0) || (telemetry == 0) ||
      (frame->type != BRIDGE_MSG_TELEMETRY) ||
      (frame->length != 48U))
  {
    return false;
  }

  telemetry->timestamp_ms = ReadU32Le(&frame->payload[offset]);
  offset += 4U;
  for (index = 0U; index < 8U; index++)
  {
    telemetry->ai_raw[index] = (int32_t)ReadU32Le(&frame->payload[offset]);
    offset += 4U;
  }
  telemetry->rtd_millicelsius = (int32_t)ReadU32Le(&frame->payload[offset]);
  offset += 4U;
  telemetry->di_bits = frame->payload[offset++];
  telemetry->relay_bits = frame->payload[offset++];
  telemetry->supply_mv[0] = ReadU16Le(&frame->payload[offset]);
  offset += 2U;
  telemetry->supply_mv[1] = ReadU16Le(&frame->payload[offset]);
  offset += 2U;
  telemetry->fault_bits = ReadU16Le(&frame->payload[offset]);
  return true;
}

bool BridgeProtocol_ParseCommandAck(const BridgeProtocolFrame *frame,
                                    BridgeProtocolAck *ack)
{
  if ((frame == 0) || (ack == 0) ||
      (frame->type != BRIDGE_MSG_COMMAND_ACK) ||
      (frame->length != 8U))
  {
    return false;
  }

  ack->request_id = ReadU16Le(&frame->payload[0]);
  ack->command_id = ReadU16Le(&frame->payload[2]);
  ack->result = (int16_t)ReadU16Le(&frame->payload[4]);
  ack->detail = ReadU16Le(&frame->payload[6]);
  return true;
}
