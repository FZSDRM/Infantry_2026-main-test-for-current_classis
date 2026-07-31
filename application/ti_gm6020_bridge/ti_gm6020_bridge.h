/**
 * @file ti_gm6020_bridge.h
 * @brief C 板专用 USART1-CAN1 GM6020 执行器从板应用。
 */

#ifndef TI_GM6020_BRIDGE_H
#define TI_GM6020_BRIDGE_H

#include <stdint.h>

extern volatile uint8_t gTiGm6020BridgeCommandOnline;
extern volatile uint8_t gTiGm6020BridgeMotorOnline;
extern volatile uint8_t gTiGm6020BridgeMotorEnabled;
extern volatile uint8_t gTiGm6020BridgeFaultActive;
extern volatile uint8_t gTiGm6020BridgeCalibrated;
extern volatile uint16_t gTiGm6020BridgeLastCommandSequence;
extern volatile uint32_t gTiGm6020BridgeValidFrameCount;
extern volatile uint32_t gTiGm6020BridgeInvalidFrameCount;
extern volatile uint32_t gTiGm6020BridgeCommandTimeoutCount;
extern volatile uint32_t gTiGm6020BridgeFeedbackFrameCount;
extern volatile uint32_t gTiGm6020BridgeFeedbackDropCount;

/**
 * @brief 将 USART1 改为 115200 8N1、注册一台 CAN1 GM6020 并保持零电流。
 * @note RobotInit 在全局中断关闭期间调用；电机收到第一帧有效反馈和第一帧使能命令前不会启用。
 */
void TiGm6020BridgeInit(void);

/**
 * @brief 消费最新串口命令、执行 50 ms 失联保护并周期回传电机状态。
 * @note 由 500 Hz RobotTask 调用；真正的 PID 与 CAN 发送仍由同频 MotorTask 独立执行。
 */
void TiGm6020BridgeTask(void);

#endif
