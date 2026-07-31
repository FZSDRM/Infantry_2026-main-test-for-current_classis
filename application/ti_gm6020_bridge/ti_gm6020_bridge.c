/**
 * @file ti_gm6020_bridge.c
 * @brief 把 TI 主控的摆杆目标转换为 C 板 GM6020 多圈位置/速度闭环。
 */

#include "ti_gm6020_bridge.h"

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "can.h"
#include "dji_motor.h"
#include "robot_def.h"
#include "rod_bridge_protocol.h"
#include "task.h"
#include "usart.h"

#define RADIANS_TO_DEGREES 57.29577951308232f
#define DEGREES_TO_RADIANS 0.017453292519943295f
#define MICRO_RADIANS_PER_RADIAN 1000000.0f

typedef struct {
    RodBridgeCommand command;
    uint16_t sequence;
} PendingBridgeCommand;

volatile uint8_t gTiGm6020BridgeCommandOnline;
volatile uint8_t gTiGm6020BridgeMotorOnline;
volatile uint8_t gTiGm6020BridgeMotorEnabled;
volatile uint8_t gTiGm6020BridgeFaultActive;
volatile uint8_t gTiGm6020BridgeCalibrated;
volatile uint16_t gTiGm6020BridgeLastCommandSequence;
volatile uint32_t gTiGm6020BridgeValidFrameCount;
volatile uint32_t gTiGm6020BridgeInvalidFrameCount;
volatile uint32_t gTiGm6020BridgeCommandTimeoutCount;
volatile uint32_t gTiGm6020BridgeFeedbackFrameCount;
volatile uint32_t gTiGm6020BridgeFeedbackDropCount;

static USARTInstance *bridge_uart;
static DJIMotorInstance *bridge_motor;
static RodBridgeParser command_parser;
static volatile PendingBridgeCommand pending_command;
static volatile uint32_t pending_generation;
static uint32_t consumed_generation;
static RodBridgeCommand active_command;
static uint16_t active_sequence;
static uint32_t last_valid_command_ms;
static uint32_t last_feedback_ms;
static uint16_t feedback_sequence;
static bool have_valid_command;
static bool calibrated;
static float motor_zero_offset_deg;
static float current_feedforward_raw;
static RodBridgeControlMode applied_mode;
static uint8_t feedback_tx_buffer[ROD_BRIDGE_MAX_FRAME_SIZE];

static float ClampFloat(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int32_t FloatToMicroRadians(float radians)
{
    float scaled = radians * MICRO_RADIANS_PER_RADIAN;

    if (scaled >= (float)INT32_MAX) {
        return INT32_MAX;
    }
    if (scaled <= (float)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static float MicroRadiansToFloat(int32_t micro_radians)
{
    return (float)micro_radians / MICRO_RADIANS_PER_RADIAN;
}

/**
 * @brief 在 USART1 DMA/IDLE 中断上下文拼接协议帧，只发布最新一条完整命令。
 * @note ISR 不调用电机接口；任务用临界区复制命令，避免读取到一半更新的多字字段。
 */
static void TiGm6020BridgeUartCallback(void)
{
    RodBridgeFrame frame;
    RodBridgeCommand decoded;

    if (bridge_uart == NULL) {
        return;
    }
    for (uint16_t index = 0U; index < bridge_uart->received_size; ++index) {
        if (!RodBridgeParser_PushByte(&command_parser,
                                      bridge_uart->recv_buff[index],
                                      &frame)) {
            continue;
        }
        if (!RodBridge_DecodeCommand(&frame, &decoded)) {
            gTiGm6020BridgeInvalidFrameCount++;
            continue;
        }
        pending_command.command = decoded;
        pending_command.sequence = frame.sequence;
        pending_generation++;
        gTiGm6020BridgeValidFrameCount++;
    }
}

static bool ConsumeLatestCommand(PendingBridgeCommand *command)
{
    bool updated = false;

    taskENTER_CRITICAL();
    if (pending_generation != consumed_generation) {
        command->command = pending_command.command;
        command->sequence = pending_command.sequence;
        consumed_generation = pending_generation;
        updated = true;
    }
    taskEXIT_CRITICAL();
    return updated;
}

static void StopMotorAndResetCalibration(void)
{
    DJIMotorSetRef(bridge_motor, 0.0f);
    DJIMotorStop(bridge_motor);
    calibrated = false;
    current_feedforward_raw = 0.0f;
    applied_mode = ROD_BRIDGE_CONTROL_STOP;
    gTiGm6020BridgeMotorEnabled = 0U;
    gTiGm6020BridgeCalibrated = 0U;
}

static bool CommandCanEstablishZero(const RodBridgeCommand *command)
{
    return ((command->flags & ROD_BRIDGE_COMMAND_ROD_STATE_VALID) != 0U) ||
           ((command->flags & ROD_BRIDGE_COMMAND_RELATIVE_ZERO) != 0U);
}

static void ApplyActiveCommand(bool motor_online)
{
    float direction_ratio =
        (float)TI_GM6020_BRIDGE_MOTOR_DIRECTION_SIGN *
        TI_GM6020_BRIDGE_TRANSMISSION_RATIO;
    float measured_rod_rad;
    float target_rod_rad;
    float target_motor_deg;
    float target_velocity_rad_s;

    if (!motor_online ||
        ((active_command.flags & ROD_BRIDGE_COMMAND_ENABLE) == 0U) ||
        (active_command.control_mode == ROD_BRIDGE_CONTROL_STOP)) {
        StopMotorAndResetCalibration();
        gTiGm6020BridgeFaultActive = motor_online ? 0U : 1U;
        return;
    }
    if (!CommandCanEstablishZero(&active_command)) {
        StopMotorAndResetCalibration();
        gTiGm6020BridgeFaultActive = 1U;
        return;
    }

    measured_rod_rad =
        (active_command.flags & ROD_BRIDGE_COMMAND_RELATIVE_ZERO) != 0U ?
            0.0f :
            MicroRadiansToFloat(active_command.measured_rod_angle_urad);
    if (!calibrated) {
        motor_zero_offset_deg = bridge_motor->measure.total_angle -
                                direction_ratio * measured_rod_rad *
                                RADIANS_TO_DEGREES;
        calibrated = true;
        gTiGm6020BridgeCalibrated = 1U;
    }

    current_feedforward_raw = ClampFloat(
        (float)active_command.current_feedforward_raw,
        -TI_GM6020_BRIDGE_CURRENT_MAX_RAW,
        TI_GM6020_BRIDGE_CURRENT_MAX_RAW);
    if (active_command.control_mode == ROD_BRIDGE_CONTROL_POSITION) {
        target_rod_rad = ClampFloat(
            MicroRadiansToFloat(active_command.target_position_urad),
            TI_GM6020_BRIDGE_ROD_MIN_RAD,
            TI_GM6020_BRIDGE_ROD_MAX_RAD);
        target_motor_deg = motor_zero_offset_deg +
                           direction_ratio * target_rod_rad *
                           RADIANS_TO_DEGREES;
        DJIMotorOuterLoop(bridge_motor, ANGLE_LOOP);
        DJIMotorSetRef(bridge_motor, target_motor_deg);
        applied_mode = ROD_BRIDGE_CONTROL_POSITION;
    } else if (active_command.control_mode == ROD_BRIDGE_CONTROL_VELOCITY) {
        target_velocity_rad_s = ClampFloat(
            MicroRadiansToFloat(active_command.target_velocity_urad_s),
            -TI_GM6020_BRIDGE_ROD_MAX_SPEED_RPS,
            TI_GM6020_BRIDGE_ROD_MAX_SPEED_RPS);
        DJIMotorOuterLoop(bridge_motor, SPEED_LOOP);
        DJIMotorSetRef(bridge_motor,
                       direction_ratio * target_velocity_rad_s *
                           RADIANS_TO_DEGREES);
        applied_mode = ROD_BRIDGE_CONTROL_VELOCITY;
    } else {
        StopMotorAndResetCalibration();
        gTiGm6020BridgeFaultActive = 1U;
        return;
    }
    DJIMotorEnable(bridge_motor);
    gTiGm6020BridgeMotorEnabled = 1U;
    gTiGm6020BridgeFaultActive = 0U;
}

static void SendFeedback(uint32_t now_ms, bool motor_online)
{
    RodBridgeFeedback feedback;
    float motor_position_rad;
    float estimated_rod_rad = 0.0f;
    size_t frame_size;

    if ((uint32_t)(now_ms - last_feedback_ms) <
        TI_GM6020_BRIDGE_FEEDBACK_PERIOD_MS) {
        return;
    }
    last_feedback_ms = now_ms;
    if (!USARTIsReady(bridge_uart)) {
        gTiGm6020BridgeFeedbackDropCount++;
        return;
    }

    motor_position_rad = bridge_motor->measure.total_angle * DEGREES_TO_RADIANS;
    if (calibrated) {
        estimated_rod_rad =
            (bridge_motor->measure.total_angle - motor_zero_offset_deg) /
            ((float)TI_GM6020_BRIDGE_MOTOR_DIRECTION_SIGN *
             TI_GM6020_BRIDGE_TRANSMISSION_RATIO) *
            DEGREES_TO_RADIANS;
    }
    memset(&feedback, 0, sizeof(feedback));
    if (motor_online) {
        feedback.flags |= ROD_BRIDGE_FEEDBACK_MOTOR_ONLINE;
    }
    if (gTiGm6020BridgeMotorEnabled != 0U) {
        feedback.flags |= ROD_BRIDGE_FEEDBACK_ENABLED;
    }
    if (gTiGm6020BridgeFaultActive != 0U) {
        feedback.flags |= ROD_BRIDGE_FEEDBACK_FAULT;
    }
    if (calibrated) {
        feedback.flags |= ROD_BRIDGE_FEEDBACK_CALIBRATED;
    }
    feedback.control_mode = applied_mode;
    feedback.accepted_sequence = active_sequence;
    feedback.motor_position_urad = FloatToMicroRadians(motor_position_rad);
    feedback.estimated_rod_angle_urad =
        FloatToMicroRadians(estimated_rod_rad);
    feedback.motor_velocity_urad_s = FloatToMicroRadians(
        bridge_motor->measure.speed_aps * DEGREES_TO_RADIANS);
    feedback.current_raw = bridge_motor->measure.real_current;
    feedback.temperature_c = bridge_motor->measure.temperate;
    frame_size = RodBridge_EncodeFeedback(feedback_sequence++,
                                          &feedback,
                                          feedback_tx_buffer,
                                          sizeof(feedback_tx_buffer));
    if (frame_size == 0U) {
        gTiGm6020BridgeFeedbackDropCount++;
        return;
    }
    USARTSend(bridge_uart,
              feedback_tx_buffer,
              (uint16_t)frame_size,
              USART_TRANSFER_DMA);
    gTiGm6020BridgeFeedbackFrameCount++;
}

void TiGm6020BridgeInit(void)
{
    USART_Init_Config_s uart_config = {
        .recv_buff_size = TI_GM6020_BRIDGE_UART_RX_BUFFER_SIZE,
        .usart_handle = &huart1,
        .module_callback = TiGm6020BridgeUartCallback,
    };
    Motor_Init_Config_s motor_config = {
        .can_init_config = {
            .can_handle = &hcan1,
            .tx_id = TI_GM6020_BRIDGE_MOTOR_ID,
        },
        .controller_param_init_config = {
            .angle_PID = {
                .Kp = TI_GM6020_BRIDGE_ANGLE_KP,
                .Ki = TI_GM6020_BRIDGE_ANGLE_KI,
                .Kd = TI_GM6020_BRIDGE_ANGLE_KD,
                .MaxOut = TI_GM6020_BRIDGE_ANGLE_MAX_SPEED_DPS,
            },
            .speed_PID = {
                .Kp = TI_GM6020_BRIDGE_SPEED_KP,
                .Ki = TI_GM6020_BRIDGE_SPEED_KI,
                .Kd = TI_GM6020_BRIDGE_SPEED_KD,
                .MaxOut = TI_GM6020_BRIDGE_CURRENT_MAX_RAW,
            },
            .current_feedforward_ptr = &current_feedforward_raw,
        },
        .controller_setting_init_config = {
            .outer_loop_type = ANGLE_LOOP,
            .close_loop_type = ANGLE_LOOP | SPEED_LOOP,
            .motor_reverse_flag = MOTOR_DIRECTION_NORMAL,
            .feedback_reverse_flag = FEEDBACK_DIRECTION_NORMAL,
            .angle_feedback_source = MOTOR_FEED,
            .speed_feedback_source = MOTOR_FEED,
            .feedforward_flag = CURRENT_FEEDFORWARD,
        },
        .motor_type = GM6020_CURRENT,
    };

    memset(&active_command, 0, sizeof(active_command));
    RodBridgeParser_Init(&command_parser);
    huart1.Init.BaudRate = TI_GM6020_BRIDGE_UART_BAUD;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    bridge_uart = USARTRegister(&uart_config);
    bridge_motor = DJIMotorInit(&motor_config);
    StopMotorAndResetCalibration();
    gTiGm6020BridgeCommandOnline = 0U;
    gTiGm6020BridgeMotorOnline = 0U;
    gTiGm6020BridgeFaultActive = 0U;
    gTiGm6020BridgeLastCommandSequence = 0U;
    gTiGm6020BridgeValidFrameCount = 0U;
    gTiGm6020BridgeInvalidFrameCount = 0U;
    gTiGm6020BridgeCommandTimeoutCount = 0U;
    gTiGm6020BridgeFeedbackFrameCount = 0U;
    gTiGm6020BridgeFeedbackDropCount = 0U;
}

void TiGm6020BridgeTask(void)
{
    PendingBridgeCommand latest;
    uint32_t now_ms = HAL_GetTick();
    bool motor_online = DJIMotorHasFeedback(bridge_motor) &&
                        DJIMotorIsOnline(bridge_motor);

    if (ConsumeLatestCommand(&latest)) {
        active_command = latest.command;
        active_sequence = latest.sequence;
        last_valid_command_ms = now_ms;
        have_valid_command = true;
        gTiGm6020BridgeCommandOnline = 1U;
        gTiGm6020BridgeLastCommandSequence = latest.sequence;
    }
    gTiGm6020BridgeMotorOnline = motor_online ? 1U : 0U;
    if (!have_valid_command ||
        ((uint32_t)(now_ms - last_valid_command_ms) >
         TI_GM6020_BRIDGE_COMMAND_TIMEOUT_MS)) {
        if (gTiGm6020BridgeCommandOnline != 0U) {
            gTiGm6020BridgeCommandTimeoutCount++;
        }
        gTiGm6020BridgeCommandOnline = 0U;
        gTiGm6020BridgeFaultActive = have_valid_command ? 1U : 0U;
        StopMotorAndResetCalibration();
    } else {
        ApplyActiveCommand(motor_online);
    }
    SendFeedback(now_ms, motor_online);
}
