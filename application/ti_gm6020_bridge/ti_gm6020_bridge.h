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
/** 板载 BMI088 是否持续产生完整 INS 样本。 */
extern volatile uint8_t gTiGm6020BridgeImuOnline;
/** 已被桥接控制任务接受的 BMI088 INS 样本数。 */
extern volatile uint32_t gTiGm6020BridgeImuSampleCount;
/** 沿摆杆正方向的去重力底盘加速度，单位 m/s²；IMU 超时后归零。 */
extern volatile float gTiGm6020BridgeChassisAccelerationMps2;

/**
 * @brief 初始化 BMI088 INS、将 USART1 改为 115200 8N1，并注册一台 CAN1 GM6020。
 * @note RobotInit 在全局中断关闭期间调用；BMI088 在线标定要求车辆静止，电机在有效命令到达前保持零电流。
 */
void TiGm6020BridgeInit(void);

/**
 * @brief 更新底盘纵向加速度、消费最新串口命令、执行失联保护并周期回传电机状态。
 * @note 由 500 Hz RobotTask 调用；真正的 PID 与 CAN 发送仍由同频 MotorTask 独立执行。
 */
void TiGm6020BridgeTask(void);

#endif
