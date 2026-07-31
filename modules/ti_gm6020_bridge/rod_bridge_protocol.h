/**
 * @file rod_bridge_protocol.h
 * @brief TI 主控与 C 板 GM6020 执行器从板之间的定长载荷串口协议。
 */

#ifndef TI_GM6020_BRIDGE_ROD_BRIDGE_PROTOCOL_H
#define TI_GM6020_BRIDGE_ROD_BRIDGE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROD_BRIDGE_SOF_FIRST 0xA5U
#define ROD_BRIDGE_SOF_SECOND 0x5AU
#define ROD_BRIDGE_PROTOCOL_VERSION 1U
#define ROD_BRIDGE_HEADER_SIZE 7U
#define ROD_BRIDGE_CRC_SIZE 2U
#define ROD_BRIDGE_MAX_PAYLOAD_SIZE 20U
#define ROD_BRIDGE_MAX_FRAME_SIZE \
    (ROD_BRIDGE_HEADER_SIZE + ROD_BRIDGE_MAX_PAYLOAD_SIZE + ROD_BRIDGE_CRC_SIZE)
#define ROD_BRIDGE_COMMAND_PAYLOAD_SIZE 16U
#define ROD_BRIDGE_FEEDBACK_PAYLOAD_SIZE 20U

typedef enum {
    ROD_BRIDGE_MESSAGE_COMMAND = 0x01U,
    ROD_BRIDGE_MESSAGE_FEEDBACK = 0x81U,
} RodBridgeMessageType;

typedef enum {
    ROD_BRIDGE_CONTROL_STOP = 0U,
    ROD_BRIDGE_CONTROL_POSITION = 1U,
    ROD_BRIDGE_CONTROL_VELOCITY = 2U,
} RodBridgeControlMode;

enum {
    ROD_BRIDGE_COMMAND_ENABLE = 1U << 0,
    ROD_BRIDGE_COMMAND_ROD_STATE_VALID = 1U << 1,
    ROD_BRIDGE_COMMAND_RELATIVE_ZERO = 1U << 2,
};

enum {
    ROD_BRIDGE_FEEDBACK_MOTOR_ONLINE = 1U << 0,
    ROD_BRIDGE_FEEDBACK_ENABLED = 1U << 1,
    ROD_BRIDGE_FEEDBACK_FAULT = 1U << 2,
    ROD_BRIDGE_FEEDBACK_CALIBRATED = 1U << 3,
};

/** 通过 CRC 后的通用帧；payload 由消息类型对应的解码函数解释。 */
typedef struct {
    uint8_t message_type;
    uint16_t sequence;
    uint8_t payload_length;
    uint8_t payload[ROD_BRIDGE_MAX_PAYLOAD_SIZE];
} RodBridgeFrame;

/** 可跨任意 DMA/中断分块持续喂入字节的接收状态。 */
typedef struct {
    uint8_t bytes[ROD_BRIDGE_MAX_FRAME_SIZE];
    uint8_t received_size;
    uint8_t expected_size;
} RodBridgeParser;

/** TI 发往 C 板的摆杆执行器命令。 */
typedef struct {
    uint8_t flags;
    RodBridgeControlMode control_mode;
    int32_t target_position_urad;
    int32_t target_velocity_urad_s;
    int32_t measured_rod_angle_urad;
    int16_t current_feedforward_raw;
} RodBridgeCommand;

/** C 板回传给 TI 的 GM6020 状态。 */
typedef struct {
    uint8_t flags;
    RodBridgeControlMode control_mode;
    uint16_t accepted_sequence;
    int32_t motor_position_urad;
    int32_t estimated_rod_angle_urad;
    int32_t motor_velocity_urad_s;
    int16_t current_raw;
    uint8_t temperature_c;
} RodBridgeFeedback;

uint16_t RodBridge_CalculateCrc16(const uint8_t *data, size_t size);
void RodBridgeParser_Init(RodBridgeParser *parser);

/**
 * @brief 向解析器送入一个字节，并在完整帧通过版本、长度和 CRC 校验后输出。
 * @note 解析状态跨 DMA/IDLE 回调保留，因此接收分块不必与串口帧边界对齐。
 */
bool RodBridgeParser_PushByte(RodBridgeParser *parser,
                              uint8_t byte,
                              RodBridgeFrame *frame);

size_t RodBridge_EncodeCommand(uint16_t sequence,
                               const RodBridgeCommand *command,
                               uint8_t *frame,
                               size_t frame_capacity);
bool RodBridge_DecodeCommand(const RodBridgeFrame *frame,
                             RodBridgeCommand *command);
size_t RodBridge_EncodeFeedback(uint16_t sequence,
                                const RodBridgeFeedback *feedback,
                                uint8_t *frame,
                                size_t frame_capacity);
bool RodBridge_DecodeFeedback(const RodBridgeFrame *frame,
                              RodBridgeFeedback *feedback);

#endif
