#include "bsp_init.h"
#include "robot.h"
#include "robot_def.h"

#if TI_GM6020_BRIDGE_MODE
#include "bsp_dwt.h"
#include "ti_gm6020_bridge.h"
#endif

// 编译warning,提醒开发者修改机器人参数
#ifndef ROBOT_DEF_PARAM_WARNING
#define ROBOT_DEF_PARAM_WARNING
#warning check if you have configured the parameters in robot_def.h, IF NOT, please refer to the comments AND DO IT, otherwise the robot will have FATAL ERRORS!!!
#endif // !ROBOT_DEF_PARAM_WARNING

#if !TI_GM6020_BRIDGE_MODE && \
    (defined(CHASSIS_BOARD) || defined(CHASSIS_ONLY))
#include "chassis.h"                     // 原速度控制底盘
#endif

#if !TI_GM6020_BRIDGE_MODE && defined(FORCE_CONTROL_CHASSIS_BOARD)
#include "chassis/chassis_force_ctrl.h"  // 力控底盘
#endif

#if !TI_GM6020_BRIDGE_MODE && defined(GIMBAL_BOARD)
#include "gimbal.h"
// #include "shoot.h" // 云台视觉专用分支不启用发射机构
#endif

#if !TI_GM6020_BRIDGE_MODE && \
    (defined(GIMBAL_BOARD) || defined(CHASSIS_ONLY) || \
     defined(FORCE_CONTROL_CHASSIS_BOARD))
#include "robot_cmd.h"
#endif

#ifdef BALANCE_BAORD
#include "balance.h"
#endif // BALANCE_BOARD


void RobotInit()
{  
    // 关闭中断,防止在初始化过程中发生中断
    // 请不要在初始化过程中使用中断和延时函数！
    // 若必须,则只允许使用DWT_Delay()
    __disable_irq();
    
#if TI_GM6020_BRIDGE_MODE
    /* PID 和 CAN 发送超时仍依赖 DWT；温控、蜂鸣器、日志等通用 BSP 不再为专用从板初始化。 */
    DWT_Init(168U);
    /* 专用从板只装配串口桥和一台 GM6020，避免原机器人应用重复注册串口或 CAN ID。 */
    TiGm6020BridgeInit();
#else
    BSPInit();
#if defined(GIMBAL_BOARD) || defined(CHASSIS_ONLY) || defined(FORCE_CONTROL_CHASSIS_BOARD)
    RobotCMDInit();
#endif

#ifdef GIMBAL_BOARD
    GimbalInit();
    // ShootInit(); // 云台视觉专用分支不启用发射机构
#endif

#if defined(CHASSIS_BOARD) || defined(CHASSIS_ONLY)
    ChassisInit();           // 原底盘初始化
#endif

#ifdef FORCE_CONTROL_CHASSIS_BOARD
    ChassisForceCtrlInit();  // 力控底盘初始化
#endif

#ifdef BALANCE_BAORD
    BalanceInit();
#endif // BALANCE_BA
#endif

    // 初始化完成,开启中断
    __enable_irq();
}

void RobotTask()
{

#if TI_GM6020_BRIDGE_MODE
    TiGm6020BridgeTask();
#else
#if defined(GIMBAL_BOARD) || defined(CHASSIS_ONLY) || defined(FORCE_CONTROL_CHASSIS_BOARD)
    RobotCMDTask();
#endif

#ifdef GIMBAL_BOARD
    GimbalTask();
    // ShootTask(); // 云台视觉专用分支不启用发射机构
#endif

#if defined(CHASSIS_BOARD) || defined(CHASSIS_ONLY)
    ChassisTask();           // 原底盘任务
#endif

#ifdef FORCE_CONTROL_CHASSIS_BOARD
    ChassisForceCtrlTask();  // 力控底盘任务
#endif

#ifdef BALANCE_BAORD
    BalanceTask();
#endif // BALANCE_BA
#endif
}
