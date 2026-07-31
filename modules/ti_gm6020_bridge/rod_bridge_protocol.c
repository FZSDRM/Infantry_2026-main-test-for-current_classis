/**
 * @file rod_bridge_protocol.c
 * @brief 与 MCU 字节序和结构体对齐无关的串口帧编解码实现。
 */

#include "rod_bridge_protocol.h"

#include <string.h>

static void WriteU16LittleEndian(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void WriteI16LittleEndian(uint8_t *destination, int16_t value)
{
    WriteU16LittleEndian(destination, (uint16_t)value);
}

static void WriteI32LittleEndian(uint8_t *destination, int32_t value)
{
    uint32_t raw = (uint32_t)value;

    destination[0] = (uint8_t)(raw & 0xFFU);
    destination[1] = (uint8_t)((raw >> 8U) & 0xFFU);
    destination[2] = (uint8_t)((raw >> 16U) & 0xFFU);
    destination[3] = (uint8_t)(raw >> 24U);
}

static uint16_t ReadU16LittleEndian(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static int16_t ReadI16LittleEndian(const uint8_t *source)
{
    return (int16_t)ReadU16LittleEndian(source);
}

static int32_t ReadI32LittleEndian(const uint8_t *source)
{
    uint32_t raw = (uint32_t)source[0] |
                   ((uint32_t)source[1] << 8U) |
                   ((uint32_t)source[2] << 16U) |
                   ((uint32_t)source[3] << 24U);

    return (int32_t)raw;
}

uint16_t RodBridge_CalculateCrc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFFU;

    if ((data == NULL) && (size != 0U)) {
        return 0U;
    }
    for (size_t index = 0U; index < size; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U ?
                      (uint16_t)((crc << 1U) ^ 0x1021U) :
                      (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

void RodBridgeParser_Init(RodBridgeParser *parser)
{
    if (parser != NULL) {
        memset(parser, 0, sizeof(*parser));
    }
}

static void ResetParserKeepingPossibleHeader(RodBridgeParser *parser,
                                              uint8_t latest_byte)
{
    parser->received_size = 0U;
    parser->expected_size = 0U;
    if (latest_byte == ROD_BRIDGE_SOF_FIRST) {
        parser->bytes[0] = latest_byte;
        parser->received_size = 1U;
    }
}

bool RodBridgeParser_PushByte(RodBridgeParser *parser,
                              uint8_t byte,
                              RodBridgeFrame *frame)
{
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint8_t payload_length;

    if ((parser == NULL) || (frame == NULL)) {
        return false;
    }
    if (parser->received_size == 0U) {
        if (byte == ROD_BRIDGE_SOF_FIRST) {
            parser->bytes[0] = byte;
            parser->received_size = 1U;
        }
        return false;
    }
    if (parser->received_size == 1U) {
        if (byte != ROD_BRIDGE_SOF_SECOND) {
            ResetParserKeepingPossibleHeader(parser, byte);
            return false;
        }
        parser->bytes[1] = byte;
        parser->received_size = 2U;
        return false;
    }
    if (parser->received_size >= ROD_BRIDGE_MAX_FRAME_SIZE) {
        ResetParserKeepingPossibleHeader(parser, byte);
        return false;
    }

    parser->bytes[parser->received_size++] = byte;
    if ((parser->received_size == 3U) &&
        (parser->bytes[2] != ROD_BRIDGE_PROTOCOL_VERSION)) {
        ResetParserKeepingPossibleHeader(parser, byte);
        return false;
    }
    if (parser->received_size == 5U) {
        payload_length = parser->bytes[4];
        if (payload_length > ROD_BRIDGE_MAX_PAYLOAD_SIZE) {
            ResetParserKeepingPossibleHeader(parser, byte);
            return false;
        }
        parser->expected_size = (uint8_t)(ROD_BRIDGE_HEADER_SIZE +
                                           payload_length +
                                           ROD_BRIDGE_CRC_SIZE);
    }
    if ((parser->expected_size == 0U) ||
        (parser->received_size < parser->expected_size)) {
        return false;
    }
    if (parser->received_size != parser->expected_size) {
        ResetParserKeepingPossibleHeader(parser, byte);
        return false;
    }

    received_crc = ReadU16LittleEndian(
        &parser->bytes[parser->expected_size - ROD_BRIDGE_CRC_SIZE]);
    calculated_crc = RodBridge_CalculateCrc16(
        parser->bytes, parser->expected_size - ROD_BRIDGE_CRC_SIZE);
    if (received_crc != calculated_crc) {
        ResetParserKeepingPossibleHeader(parser, byte);
        return false;
    }

    frame->message_type = parser->bytes[3];
    frame->payload_length = parser->bytes[4];
    frame->sequence = ReadU16LittleEndian(&parser->bytes[5]);
    memset(frame->payload, 0, sizeof(frame->payload));
    memcpy(frame->payload,
           &parser->bytes[ROD_BRIDGE_HEADER_SIZE],
           frame->payload_length);
    parser->received_size = 0U;
    parser->expected_size = 0U;
    return true;
}

static size_t EncodeFrame(uint8_t message_type,
                          uint16_t sequence,
                          const uint8_t *payload,
                          uint8_t payload_size,
                          uint8_t *frame,
                          size_t frame_capacity)
{
    size_t frame_size = ROD_BRIDGE_HEADER_SIZE + payload_size + ROD_BRIDGE_CRC_SIZE;
    uint16_t crc;

    if ((frame == NULL) || (payload == NULL) ||
        (payload_size > ROD_BRIDGE_MAX_PAYLOAD_SIZE) ||
        (frame_capacity < frame_size)) {
        return 0U;
    }
    frame[0] = ROD_BRIDGE_SOF_FIRST;
    frame[1] = ROD_BRIDGE_SOF_SECOND;
    frame[2] = ROD_BRIDGE_PROTOCOL_VERSION;
    frame[3] = message_type;
    frame[4] = payload_size;
    WriteU16LittleEndian(&frame[5], sequence);
    memcpy(&frame[ROD_BRIDGE_HEADER_SIZE], payload, payload_size);
    crc = RodBridge_CalculateCrc16(frame, frame_size - ROD_BRIDGE_CRC_SIZE);
    WriteU16LittleEndian(&frame[frame_size - ROD_BRIDGE_CRC_SIZE], crc);
    return frame_size;
}

size_t RodBridge_EncodeCommand(uint16_t sequence,
                               const RodBridgeCommand *command,
                               uint8_t *frame,
                               size_t frame_capacity)
{
    uint8_t payload[ROD_BRIDGE_COMMAND_PAYLOAD_SIZE];

    if ((command == NULL) || (command->control_mode > ROD_BRIDGE_CONTROL_VELOCITY)) {
        return 0U;
    }
    payload[0] = command->flags;
    payload[1] = (uint8_t)command->control_mode;
    WriteI32LittleEndian(&payload[2], command->target_position_urad);
    WriteI32LittleEndian(&payload[6], command->target_velocity_urad_s);
    WriteI32LittleEndian(&payload[10], command->measured_rod_angle_urad);
    WriteI16LittleEndian(&payload[14], command->current_feedforward_raw);
    return EncodeFrame(ROD_BRIDGE_MESSAGE_COMMAND, sequence, payload,
                       sizeof(payload), frame, frame_capacity);
}

bool RodBridge_DecodeCommand(const RodBridgeFrame *frame,
                             RodBridgeCommand *command)
{
    if ((frame == NULL) || (command == NULL) ||
        (frame->message_type != ROD_BRIDGE_MESSAGE_COMMAND) ||
        (frame->payload_length != ROD_BRIDGE_COMMAND_PAYLOAD_SIZE) ||
        (frame->payload[1] > ROD_BRIDGE_CONTROL_VELOCITY)) {
        return false;
    }
    command->flags = frame->payload[0];
    command->control_mode = (RodBridgeControlMode)frame->payload[1];
    command->target_position_urad = ReadI32LittleEndian(&frame->payload[2]);
    command->target_velocity_urad_s = ReadI32LittleEndian(&frame->payload[6]);
    command->measured_rod_angle_urad = ReadI32LittleEndian(&frame->payload[10]);
    command->current_feedforward_raw = ReadI16LittleEndian(&frame->payload[14]);
    return true;
}

size_t RodBridge_EncodeFeedback(uint16_t sequence,
                                const RodBridgeFeedback *feedback,
                                uint8_t *frame,
                                size_t frame_capacity)
{
    uint8_t payload[ROD_BRIDGE_FEEDBACK_PAYLOAD_SIZE];

    if ((feedback == NULL) || (feedback->control_mode > ROD_BRIDGE_CONTROL_VELOCITY)) {
        return 0U;
    }
    payload[0] = feedback->flags;
    payload[1] = (uint8_t)feedback->control_mode;
    WriteU16LittleEndian(&payload[2], feedback->accepted_sequence);
    WriteI32LittleEndian(&payload[4], feedback->motor_position_urad);
    WriteI32LittleEndian(&payload[8], feedback->estimated_rod_angle_urad);
    WriteI32LittleEndian(&payload[12], feedback->motor_velocity_urad_s);
    WriteI16LittleEndian(&payload[16], feedback->current_raw);
    payload[18] = feedback->temperature_c;
    payload[19] = 0U;
    return EncodeFrame(ROD_BRIDGE_MESSAGE_FEEDBACK, sequence, payload,
                       sizeof(payload), frame, frame_capacity);
}

bool RodBridge_DecodeFeedback(const RodBridgeFrame *frame,
                              RodBridgeFeedback *feedback)
{
    if ((frame == NULL) || (feedback == NULL) ||
        (frame->message_type != ROD_BRIDGE_MESSAGE_FEEDBACK) ||
        (frame->payload_length != ROD_BRIDGE_FEEDBACK_PAYLOAD_SIZE) ||
        (frame->payload[1] > ROD_BRIDGE_CONTROL_VELOCITY)) {
        return false;
    }
    feedback->flags = frame->payload[0];
    feedback->control_mode = (RodBridgeControlMode)frame->payload[1];
    feedback->accepted_sequence = ReadU16LittleEndian(&frame->payload[2]);
    feedback->motor_position_urad = ReadI32LittleEndian(&frame->payload[4]);
    feedback->estimated_rod_angle_urad = ReadI32LittleEndian(&frame->payload[8]);
    feedback->motor_velocity_urad_s = ReadI32LittleEndian(&frame->payload[12]);
    feedback->current_raw = ReadI16LittleEndian(&frame->payload[16]);
    feedback->temperature_c = frame->payload[18];
    return true;
}
