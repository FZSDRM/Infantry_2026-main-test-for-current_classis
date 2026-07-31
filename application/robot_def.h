/**
 * @file robot_def.h
 * @author NeoZeng neozng1@hnu.edu.cn
 * @author Even
 * @version 0.1
 * @date 2022-12-02
 *
 * @copyright Copyright (c) HNU YueLu EC 2022 all rights reserved
 *
 */
#pragma once // 可以用#pragma once代替#ifndef ROBOT_DEF_H(header guard)
#ifndef ROBOT_DEF_H
#define ROBOT_DEF_H

#include "ins_task.h"
#include "master_process.h"
#include "stdint.h"

/* 开发板类型定义,烧录时注意不要弄错对应功能;修改定义后需要重新编译,只能存在一个定义! */
//#define CHASSIS_BOARD                 // 底盘板 (速控底盘)
#define GIMBAL_BOARD                    // 云台板
//#define CHASSIS_ONLY                  // 底盘调试模式: 无云台,只有底盘+超级电容+遥控器 
//#define FORCE_CONTROL_CHASSIS_BOARD   // 力控底盘板

/* ================= TI 主控 -> C 板 -> GM6020 专用执行器模式 =================
 * 置 1 后，C 板不再运行遥控器、旧视觉、裁判系统和原云台应用，只保留：
 * 1. InsTask：1 kHz 读取板载 BMI088，输出去重力的车体运动加速度；
 * 2. RobotTask：解析 USART1 控制帧、执行失联保护并回传电机状态；
 * 3. MotorTask：500 Hz 位置/速度闭环并通过 CAN1 发送 GM6020 电流；
 * 4. DaemonTask：100 Hz 检查 GM6020 CAN 反馈是否离线。
 * 置 0 可恢复当前分支原有的机器人应用装配，桥接模块仍保留在源码中。
 */
#define TI_GM6020_BRIDGE_MODE                 1U
/* TI 的 PB0/TX 接 C 板 PB7/USART1_RX；C 板 PA9/USART1_TX 接 TI PB1/RX，双方共地。 */
#define TI_GM6020_BRIDGE_UART_BAUD             115200U
/* USART1 DMA 单次接收容量；协议解析器会跨 IDLE/DMA 分块拼帧，不要求一包一次收齐。 */
#define TI_GM6020_BRIDGE_UART_RX_BUFFER_SIZE   64U
/* TI 正常以 200 Hz 发命令；超过该时间没有有效 CRC 帧时立即发送零电流并取消标定。 */
#define TI_GM6020_BRIDGE_COMMAND_TIMEOUT_MS    50U
/* C 板状态回传周期；20 ms 即 50 Hz，兼顾 OLED/调试可见性和 115200 波特率余量。 */
#define TI_GM6020_BRIDGE_FEEDBACK_PERIOD_MS    20U
/* GM6020 挂在 C 板 CAN1，拨码 ID 范围为 1～7；当前默认使用 ID1。 */
#define TI_GM6020_BRIDGE_MOTOR_ID              1U
/* 电机轴正方向相对“摆杆正角度”的符号，只允许 +1 或 -1；方向错误只改这里。 */
#define TI_GM6020_BRIDGE_MOTOR_DIRECTION_SIGN  1
/* 电机轴转角 / 摆杆角度；直驱填 1，存在减速或同步带时填实际正数传动比。 */
#define TI_GM6020_BRIDGE_TRANSMISSION_RATIO    1.0f
/* C 板第二道机械角保护，单位 rad；范围应覆盖正式 ±0.20 rad 和既有 B21 抬升测试。 */
#define TI_GM6020_BRIDGE_ROD_MIN_RAD           (-1.40f)
#define TI_GM6020_BRIDGE_ROD_MAX_RAD           0.25f
/* 速度模式绝对上限，单位 rad/s；超出协议目标会在 C 板侧钳位。 */
#define TI_GM6020_BRIDGE_ROD_MAX_SPEED_RPS     3.2f
/* 位置外环：输入/反馈为电机多圈角度（°），输出为速度参考（°/s）。 */
#define TI_GM6020_BRIDGE_ANGLE_KP              8.0f
#define TI_GM6020_BRIDGE_ANGLE_KI              0.0f
#define TI_GM6020_BRIDGE_ANGLE_KD              0.0f
#define TI_GM6020_BRIDGE_ANGLE_MAX_SPEED_DPS   180.0f
/* 速度内环：输入/反馈为电机角速度（°/s），输出为 GM6020 0x1FE 电流原始值。 */
#define TI_GM6020_BRIDGE_SPEED_KP              100.0f
#define TI_GM6020_BRIDGE_SPEED_KI              0.0f
#define TI_GM6020_BRIDGE_SPEED_KD              0.0f
/* 首次上板采用较保守的 3000 raw 限流；确认方向和机构无干涉后再逐步提高。 */
#define TI_GM6020_BRIDGE_CURRENT_MAX_RAW       3000.0f
/*
 * BMI088 去重力机体系加速度中与摆杆平行的轴：0=X、1=Y、2=Z。
 * 当前工程底盘代码把 `-MotionAccel_b[0]` 定义为车头正向，因此默认选择 X 轴并取反。
 */
#define TI_GM6020_BRIDGE_CHASSIS_ACCEL_AXIS    0U
#define TI_GM6020_BRIDGE_CHASSIS_ACCEL_SIGN    (-1)
/* BMI088 连续多长时间没有完成新解算后判为无效；10 ms 覆盖数个 1 kHz 任务周期。 */
#define TI_GM6020_BRIDGE_IMU_TIMEOUT_MS         10U
/* 本地平衡算法可使用的底盘加速度绝对上限，单位 m/s²；异常样本先在输入边界钳位。 */
#define TI_GM6020_BRIDGE_CHASSIS_ACCEL_MAX_MPS2 20.0f

#if (TI_GM6020_BRIDGE_MODE > 1U) || \
    (TI_GM6020_BRIDGE_MOTOR_ID < 1U) || \
    (TI_GM6020_BRIDGE_MOTOR_ID > 7U) || \
    (TI_GM6020_BRIDGE_CHASSIS_ACCEL_AXIS > 2U) || \
    ((TI_GM6020_BRIDGE_MOTOR_DIRECTION_SIGN != 1) && \
     (TI_GM6020_BRIDGE_MOTOR_DIRECTION_SIGN != -1)) || \
    ((TI_GM6020_BRIDGE_CHASSIS_ACCEL_SIGN != 1) && \
     (TI_GM6020_BRIDGE_CHASSIS_ACCEL_SIGN != -1)) || \
    (TI_GM6020_BRIDGE_IMU_TIMEOUT_MS == 0U)
#error Invalid TI_GM6020 bridge configuration
#endif

/* GM6020 ID 1-7 遍历模式；注释首行即可恢复原 yaw/pitch 云台控制。 */
#define GM6020_ID_SCAN_MODE
#define GM6020_ID_SCAN_FIRST_ID          1u
#define GM6020_ID_SCAN_LAST_ID           7u
#define GM6020_ID_SCAN_MOTOR_COUNT       (GM6020_ID_SCAN_LAST_ID - GM6020_ID_SCAN_FIRST_ID + 1u)
#define GM6020_ID_SCAN_MAX_SPEED_DPS     10.0f
#define GM6020_ID_SCAN_TRAVEL_DEG        10.0f
#define GM6020_ID_SCAN_ORIGIN_TOL_DEG    0.5f
#define GM6020_ID_SCAN_ONLINE_TIMEOUT_MS 1000u
#define GM6020_ID_SCAN_MOTION_TIMEOUT_MS 3000u
#define GM6020_ID_SCAN_SETTLE_MS         300u
#define GM6020_ID_SCAN_CURRENT_MAX_RAW   3000.0f


/* 遥控器类型选择: 定义USE_IMAGE_REMOTE使用图传遥控器(UART6), 注释掉则使用原DJI遥控器(USART3/DBUS) */
//#define USE_IMAGE_REMOTE

// 视觉通信协议选择,只能开一个

#define VISION_USE_VCP          // USB虚拟串口
// #define VISION_USE_UART         // 串口+seasky协议
// #define VISION_USE_SERIALPORT   // HUST上位机
//#define VISION_USE_SP               // SP协议,支持前馈

// 协议兼容性检查 - 确保只定义了一个协议
#if (defined(VISION_USE_VCP) + defined(VISION_USE_UART) + defined(VISION_USE_SERIALPORT) + defined(VISION_USE_SP)) > 1
#error "Error: Multiple vision protocols defined! Please select only ONE protocol."
#endif

#if !defined(VISION_USE_VCP) && !defined(VISION_USE_UART) && !defined(VISION_USE_SERIALPORT) && !defined(VISION_USE_SP)
#error "Error: No vision protocol defined! Please select one protocol."
#endif

/* ======================== 视觉目标插值参数 ======================== */
#define VISION_INTERP_DURATION_MS      20.0f    // 视觉目标线性插值时长(ms)
#define VISION_INTERP_EPSILON_DEG      0.1f    // 视觉目标更新判定阈值(°)

//#define USE_LASER_POINTER  // 启用激光笔模式,注释掉此行则使用完整发射机构

// @todo: 增加机器人类型定义,后续是否要兼容所有机器人?(只兼容步兵英雄哨兵似乎就够了)
// 通过该宏,你可以直接将所有机器人的参数保存在一处,然后每次只需要修改这个宏就可以替换所有参数
/* 机器人类型定义 */
// #define ROBOT_HERO 1     // 英雄机器人
// #define ROBOT_ENINEER 2  // 工程机器人
#define ROBOT_INFANTRY 3 // 步兵机器人3
// #define ROBOT_INFANTRY 4 // 步兵机器人4
// #define ROBOT_INFANTRY 5 // 步兵机器人5
// #define ROBOT_SENTRY 6   // 哨兵机器人

/* 机器人重要参数定义,注意根据不同机器人进行修改,浮点数需要以.0或f结尾,无符号以u结尾 */
// 云台参数
#define YAW_ALIGN_ECD         0 //0    //云台和底盘对齐指向相同方向时的yaw的差值,需要测量
//#define YAW_CHASSIS_ALIGN_ECD 5526  //步兵正  // 云台和底盘对齐指向相同方向时的电机编码器值,若对云台有机械改动需要修改 
#define YAW_CHASSIS_ALIGN_ECD 1383  //步兵反
#define YAW_ECD_GREATER_THAN_4096 0 // ALIGN_ECD值是否大于4096,是为1,否为0;用于计算云台偏转角度
#define PITCH_HORIZON_ECD 3625       // 云台处于水平位置时编码器值,若对云台有机械改动需要修改
#define PITCH_MAX_ANGLE 0 //云台竖直方向最大角度 (注意反馈如果是陀螺仪，则填写陀螺仪的角度) 
#define PITCH_MIN_ANGLE 0 //云台竖直方向最小角度 (注意反馈如果是陀螺仪，则填写陀螺仪的角度)

/* ======================== 云台软件限位 ======================== */
#define PITCH_MIN_LIMIT         -20.0f  // pitch最小角度(°)
#define PITCH_MAX_LIMIT         30.0f   // pitch最大角度(°)

/* ======================== 参数辨识模式 ======================== */
#define IDENT_MODE_ROTATE              1        // 右开关下档: 保持原小陀螺模式
#define IDENT_MODE_YAW                 2        // 右开关下档: yaw参数辨识模式
#define IDENT_MODE_PITCH               3        // 右开关下档: pitch参数辨识模式
#define IDENT_MODE_SPIN                4        // 右开关下档: 底盘自旋辨识模式
#define IDENT_MODE_YAW_SINE            5        // 右开关下档: yaw小范围正弦辨识模式(50Hz刷新)
#define IDENT_MODE_ENABLE              IDENT_MODE_ROTATE

/* ======================== Yaw参数辨识模式 ======================== */
#define YAW_IDENT_START_SPEED_DPS      150.0f   // 初始恒速平台角速度 (°/s)
#define YAW_IDENT_SPEED_STEP_DPS       50.0f    // 每轮辨识后的角速度增量 (°/s)
#define YAW_IDENT_MAX_SPEED_DPS        900.0f   // 最大恒速平台角速度 (°/s)
#define YAW_IDENT_PLATFORM_MS          900.0f   // 单边恒速平台时长(ms)
#define YAW_IDENT_HOLD_MS              250.0f   // 每段之间保持时间(ms)
#define YAW_SINE_IDENT_AMPLITUDE_DEG   8.0f     // yaw正弦辨识幅值(°)
#define YAW_SINE_IDENT_FREQ_HZ         1.5f     // yaw正弦辨识频率(Hz)
#define YAW_SINE_IDENT_UPDATE_MS       20.0f    // yaw正弦辨识控制量刷新周期(ms), 50Hz

/* ======================== Pitch参数辨识模式 ======================== */
#define PITCH_IDENT_START_SPEED_DPS    80.0f    // 初始恒速平台角速度 (°/s)
#define PITCH_IDENT_SPEED_STEP_DPS     20.0f    // 每轮辨识后的角速度增量 (°/s)
#define PITCH_IDENT_MAX_SPEED_DPS      220.0f   // 最大恒速平台角速度 (°/s)
#define PITCH_IDENT_PLATFORM_MS        700.0f   // 单边恒速平台时长(ms)
#define PITCH_IDENT_HOLD_MS            250.0f   // 每段之间保持时间(ms)

/* ======================== 自旋参数辨识模式 ======================== */
#define SPIN_IDENT_START_SPEED_DPS     1500.0f  // 初始底盘自旋角速度 (°/s)
#define SPIN_IDENT_SPEED_STEP_DPS      500.0f   // 每轮辨识后的角速度增量 (°/s)
#define SPIN_IDENT_MAX_SPEED_DPS       5000.0f  // 最大底盘自旋角速度 (°/s)
#define SPIN_IDENT_PLATFORM_MS         1200.0f  // 单边恒速平台时长(ms)
#define SPIN_IDENT_HOLD_MS             300.0f   // 每段之间保持时间(ms)

// 拨盘堵转检测与反转参数
#define BLOCK_DETECT_THRESHOLD 0.70f   // 堵转判定误差阈值(误差/目标值)
#define BLOCK_DETECT_COUNT 999         // 堵转判定次数
#define BLOCK_SPEED_THRESHOLD  18000.0f  // 拨盘堵转辅助判定: 低转速阈值 (degree/s, 电机转子)
#define BLOCK_CURRENT_THRESHOLD 9000   // 拨盘堵转辅助判定: 高反馈电流阈值 (DJI原始反馈值)
#define BLOCK_TIME_RECORD_COUNT 20     // 记录堵转时间戳的计数阈值，不建议修改 20
#define REVERSE_DURATION_MS 80          // 反转持续时间(ms)
#define REVERSE_ANGLE_RATIO 0.8f       // 反转角度系数，不建议修改 1.0
#define SHOOT_FRICTION_SOFT_STOP_MS 600.0f // 摩擦轮关闭时先速度闭环到0再停机的时间(ms)

// 发射参数
#define ONE_BULLET_DELTA_ANGLE 36   // 发射一发弹丸拨盘转动的距离,由机械设计图纸给出
#define REDUCTION_RATIO_LOADER 36.0f // 拨盘电机的减速比,2006为36.0f
#define NUM_PER_CIRCLE 10             // 拨盘一圈的装载量
// 机器人底盘修改的参数,单位为mm(毫米)
#define WHEEL_BASE 311              // 纵向轴距(前进后退方向)
#define TRACK_WIDTH 311             // 横向轮距(左右平移方向)
#define WHEEL_DIAGONAL (sqrt((double)(WHEEL_BASE^2) + (double)(TRACK_WIDTH^2))/2)
#define CENTER_GIMBAL_OFFSET_X 0    // 云台旋转中心距底盘几何中心的距离,前后方向,云台位于正中心时默认设为0
#define CENTER_GIMBAL_OFFSET_Y 0    // 云台旋转中心距底盘几何中心的距离,左右方向,云台位于正中心时默认设为0
#define RADIUS_WHEEL 75             // 轮子半径
#define REDUCTION_RATIO_WHEEL 15.7f // 电机减速比,因为编码器量测的是转子的速度而不是输出轴的速度故需进行转换

/* ======================== 底盘IMU配置 ======================== */
// IMU安装位置偏移 (相对于底盘几何中心, 单位mm)
// 正方向: X轴指向机器人前方, Y轴指向机器人左侧
#define CHASSIS_IMU_OFFSET_X     -185.0f   // IMU相对底盘中心的X偏移 (mm), 正值表示IMU在前
#define CHASSIS_IMU_OFFSET_Y     4.1f   // IMU相对底盘中心的Y偏移 (mm), 正值表示IMU在左

#define CHASSIS_POWER_LIMIT 100 // 
/* ======================== 底盘平动参数 ======================== */    
#define CHASSIS_TRANSLATE_BASE_SPEED  40000.0f  // 键鼠模式基础平动速度 (degree/s)
#define CHASSIS_TRANSLATE_DASH_RATIO  5.0f     // 按住Shift时的平动加速倍率

/* ======================== 底盘自旋参数 ======================== */
#define CHASSIS_ROTATE_BASE_WZ       5000.0f  // 小陀螺基础自旋速度 (degree/s)
#define CHASSIS_ROTATE_DASH_RATIO    1.7f    // 键鼠模式按住Shift时的小陀螺倍率

/* ======================== 发射弹速闭环参数 ======================== */
#define SHOOT_BULLET_SPEED_TARGET      22.5f     // 目标弹速 (m/s)
#define SHOOT_FRICTION_BASE_REF        37000.0f  // 摩擦轮基础目标转速
#define SHOOT_FRICTION_REF_MIN         16000.0f  // 摩擦轮闭环最小目标转速
#define SHOOT_FRICTION_REF_MAX         39000.0f  // 摩擦轮闭环最大目标转速
#define SHOOT_FRICTION_SPEED_KP        100.0f   // 每次有效弹速反馈的增量校正系数
#define SHOOT_BULLET_SPEED_DEADBAND    0.2f      // 弹速控制死区

/* ======================== IMU安装方向校正 ======================== */
// 使用ins_task中的IMU_Param_Correction进行方向校正
// Yaw/Pitch/Roll: IMU安装相对于机体系的偏角 (度)
// scale: 陀螺仪标度因数校正 (默认1.0)

#if defined(CHASSIS_BOARD) || defined(CHASSIS_ONLY) || defined(FORCE_CONTROL_CHASSIS_BOARD)
// 底盘板IMU安装方向校正 (速控/力控/调试模式通用)
#define IMU_PARAM_YAW        0.0f    // IMU Yaw偏角 (°)
#define IMU_PARAM_PITCH      0.0f    // IMU Pitch偏角 (°)
#define IMU_PARAM_ROLL       0.0f    // IMU Roll偏角 (°)
#define IMU_PARAM_SCALE_X    1.0f    // 陀螺仪 X轴标度因数
#define IMU_PARAM_SCALE_Y    1.0f    // 陀螺仪 Y轴标度因数
#define IMU_PARAM_SCALE_Z    1.0f    // 陀螺仪 Z轴标度因数
#endif

#ifdef GIMBAL_BOARD
// 云台板IMU安装方向校正
#define IMU_PARAM_YAW        0.0f    // IMU Yaw偏角 (°)
#define IMU_PARAM_PITCH      0.0f    // IMU Pitch偏角 (°)
#define IMU_PARAM_ROLL       0.0f    // IMU Roll偏角 (°)
#define IMU_PARAM_SCALE_X    1.0f    // 陀螺仪 X轴标度因数
#define IMU_PARAM_SCALE_Y    1.0f    // 陀螺仪 Y轴标度因数
#define IMU_PARAM_SCALE_Z    1.0f    // 陀螺仪 Z轴标度因数
#endif


/* ======================== GM6020电流模式物理常数 ======================== */
// GM6020电流模式: 反馈real_current [-16384, +16384] 对应 [-3A, +3A]
//                 发送CAN指令也是同比例的电流值 (int16)
#define GM6020_KT               0.741f   // 转矩常数 (N·m/A)
#define GM6020_I_MAX             3.0f     // 最大电流 (A)
#define GM6020_RAW_MAX           16384.0f // CAN原始值满量程
// N·m → CAN原始值的统一转换系数: raw = τ / Kt * (16384 / 3)
#define NM_TO_GM6020_RAW        (GM6020_RAW_MAX / (GM6020_I_MAX * GM6020_KT))  // ≈7371.1

/* ======================== 云台前馈参数 ======================== */
// 前馈在gimbal中本地计算。yaw链路使用工程/raw域参数，直接输出GM6020电流原始值。
// 速度前馈系数 (目标角速度 * 系数 = 速度前馈输出)
#define YAW_SPEED_FF_COEF       0.5f     // yaw速度前馈系数
#define PITCH_SPEED_FF_COEF     0.5f     // pitch速度前馈系数 
// yaw目标运动前馈: 直接在raw域建模，使用参考角速度/角加速度，不依赖物理扭矩单位
#define YAW_ACC_FF_COEF_RAW_POS           0.0f   // yaw正向目标运动加速度项 (GM6020 raw per rad/s²)
#define YAW_MOTION_W_FF_COEF_RAW_POS      0.0f       // yaw正向目标运动粘滞项 (GM6020 raw per rad/s)
#define YAW_MOTION_FRICTION_C_FF_RAW_POS  0.0f          // yaw正向目标运动库仑摩擦项 (GM6020 raw)
#define YAW_MOTION_FRICTION_S_FF_RAW_POS  0.0f          // yaw正向目标运动Stribeck静摩擦项 (GM6020 raw)
#define YAW_MOTION_BIAS_FF_RAW_POS        0.0f        // yaw正向目标运动常值偏置项 (GM6020 raw)
#define YAW_ACC_FF_COEF_RAW_NEG           0.0f   // yaw反向目标运动加速度项 (GM6020 raw per rad/s²)
#define YAW_MOTION_W_FF_COEF_RAW_NEG      0.0f       // yaw反向目标运动粘滞项 (GM6020 raw per rad/s)
#define YAW_MOTION_FRICTION_C_FF_RAW_NEG  0.0f          // yaw反向目标运动库仑摩擦项 (GM6020 raw)
#define YAW_MOTION_FRICTION_S_FF_RAW_NEG  0.0f          // yaw反向目标运动Stribeck静摩擦项 (GM6020 raw)
#define YAW_MOTION_BIAS_FF_RAW_NEG        0.0f        // yaw反向目标运动常值偏置项 (GM6020 raw)
#define YAW_MOTION_STRIBECK_VS_RAD    2.909719f  // 目标运动Stribeck特征速度 (rad/s)
// pitch前馈同样改为raw域工程参数，重力项单独使用姿态补偿
#define PITCH_ACC_FF_COEF_RAW_POS   0.0f     // pitch正向加速度项 (GM6020 raw per rad/s²)
#define PITCH_W_FF_COEF_RAW_POS     0.0f     // pitch正向速度项 (GM6020 raw per rad/s)
#define PITCH_BIAS_FF_RAW_POS       0.0f     // pitch正向常值偏置项 (GM6020 raw)
#define PITCH_ACC_FF_COEF_RAW_NEG   0.0f     // pitch反向加速度项 (GM6020 raw per rad/s²)
#define PITCH_W_FF_COEF_RAW_NEG     0.0f     // pitch反向速度项 (GM6020 raw per rad/s)
#define PITCH_BIAS_FF_RAW_NEG       0.0f     // pitch反向常值偏置项 (GM6020 raw)

/* ======================== Yaw底盘自旋补偿参数 (Stribeck raw域模型) ======================== */
// ff_spin_raw = bias_raw + b_raw*ω + tau_c_raw*scale_c(ω, v_s) + tau_s_raw*scale_s(ω, v_s)
// 直接在GM6020 raw域拟合，避免依赖严格物理扭矩单位
#define YAW_WZ_FF_COEF_RAW_POS      0.0f     // 正向粘滞项系数 (GM6020 raw per rad/s)
#define YAW_FRICTION_C_FF_RAW_POS   0.0f     // 正向库仑摩擦项 (GM6020 raw)
#define YAW_FRICTION_S_FF_RAW_POS   0.0f     // 正向Stribeck静摩擦项 (GM6020 raw)
#define YAW_BIAS_FF_RAW_POS         0.0f     // 正向常值偏置项 (GM6020 raw)
#define YAW_WZ_FF_COEF_RAW_NEG      0.0f     // 反向粘滞项系数 (GM6020 raw per rad/s)
#define YAW_FRICTION_C_FF_RAW_NEG   0.0f     // 反向库仑摩擦项 (GM6020 raw)
#define YAW_FRICTION_S_FF_RAW_NEG   0.0f     // 反向Stribeck静摩擦项 (GM6020 raw)
#define YAW_BIAS_FF_RAW_NEG         0.0f     // 反向常值偏置项 (GM6020 raw)
#define YAW_STRIBECK_VS_RAD     0.08f    // Stribeck特征速度 (rad/s)

/* ======================== Pitch重力补偿参数 ======================== */
#define GRAVITY_COMP_MAX        0.0f  // 最大重力补偿 (CAN原始值,水平时)
#define PITCH_HORIZONTAL_ANGLE  -1.2f      // pitch水平时的IMU角度(°)

// 检查是否出现主控板定义冲突,只允许一个开发板定义存在,否则编译会自动报错
#if (defined(CHASSIS_BOARD) + defined(GIMBAL_BOARD) + defined(CHASSIS_ONLY) + defined(FORCE_CONTROL_CHASSIS_BOARD)) != 1
#error Conflict board definition! You must define exactly ONE board type (CHASSIS_BOARD, GIMBAL_BOARD, CHASSIS_ONLY, or FORCE_CONTROL_CHASSIS_BOARD).
#endif

#pragma pack(1) // 压缩结构体,取消字节对齐,下面的数据都可能被传输
/* -------------------------基本控制模式和数据类型定义-------------------------*/
/**
 * @brief 这些枚举类型和结构体会作为CMD控制数据和各应用的反馈数据的一部分
 *
 */
// 机器人状态
typedef enum
{
    ROBOT_STOP = 0,
    ROBOT_READY,
} Robot_Status_e;

// 应用状态
typedef enum
{
    APP_OFFLINE = 0,
    APP_ONLINE,
    APP_ERROR,
} App_Status_e;

// 底盘模式设置
/**
 * @brief 后续考虑修改为云台跟随底盘,而不是让底盘去追云台,云台的惯量比底盘小.
 *
 */
typedef enum
{
    CHASSIS_ZERO_FORCE = 0,    // 电流零输入
    CHASSIS_ROTATE,            // 小陀螺模式
    CHASSIS_NO_FOLLOW,         // 不跟随，允许全向平移
    CHASSIS_FOLLOW_GIMBAL_YAW, // 跟随模式，底盘叠加角度环控制
} chassis_mode_e;

// 云台模式设置
typedef enum
{
    GIMBAL_ZERO_FORCE = 0, // 电流零输入
    GIMBAL_FREE_MODE,      // 云台自由运动模式,即与底盘分离(底盘此时应为NO_FOLLOW)反馈值为电机total_angle;似乎可以改为全部用IMU数据?
    GIMBAL_GYRO_MODE,      // 云台陀螺仪反馈模式,反馈值为陀螺仪pitch,total_yaw_angle,底盘可以为小陀螺和跟随模式
} gimbal_mode_e;

// 发射模式设置
typedef enum
{
    SHOOT_OFF = 0,
    SHOOT_ON,
} shoot_mode_e;
typedef enum
{
    FRICTION_OFF = 0, // 摩擦轮关闭
    FRICTION_ON,      // 摩擦轮开启
} friction_mode_e;

typedef enum
{
    LOAD_STOP = 0,  // 停止发射
    LOAD_REVERSE,   // 反转
    LOAD_1_BULLET,  // 单发
    LOAD_3_BULLET,  // 三发
    LOAD_4_BULLET, // 四发
    LOAD_5_BULLET, // 五发
    LOAD_BURSTFIRE, // 连发
} loader_mode_e;

typedef enum
{
    UI_KEEP,
    UI_REFRESH,
} ui_mode_e;
typedef enum
{
    FIRE_OFF = 0,
    FIRE_ON,
} fire_mode_e;
// 功率限制,从裁判系统获取,是否有必要保留?
typedef struct
{ // 功率控制
    float chassis_power_mx;
} Chassis_Power_Data_s;
typedef enum
{ // 超电启停
    CAP_OFF,
    CAP_ON,
} SuperCap_Mode_e;

// 自瞄模式设置
typedef enum
{
    AIM_OFF = 0,  // 开启自瞄模式
    AIM_ON,   
} aim_mode_e;

typedef enum
{
    DASH_OFF = 0,  // 开启自瞄模式
    DASH_ON,   
} dash_mode_e;

/* ----------------CMD应用发布的控制数据,应当由gimbal/chassis/shoot订阅---------------- */
/**
 * @brief 对于双板情况,遥控器和pc在云台,裁判系统在底盘
 *
 */
// cmd发布的底盘控制数据,由chassis订阅
typedef struct
{
    // 控制部分
    float vx;           // 前进方向速度, 单位: degree/s (电机转子角速度)
    float vy;           // 横移方向速度, 单位: degree/s (电机转子角速度)
    float wz;           // 旋转速度, 单位: degree/s
    float offset_angle; // 底盘和归中位置的夹角, 单位: degree
    chassis_mode_e chassis_mode;
    dash_mode_e dash_mode; // 冲刺模式

    // UI部分
    ui_mode_e ui_mode;
    gimbal_mode_e gimbal_mode;
    friction_mode_e friction_mode;
    loader_mode_e load_mode;
    SuperCap_Mode_e super_cap;
    // float pitch_angle;
    aim_mode_e aim_mode;
    fire_mode_e fire_mode;

} Chassis_Ctrl_Cmd_s;

// cmd发布的云台控制数据,由gimbal订阅
typedef struct
{ // 云台角度控制
    float yaw;
    float pitch;
    float chassis_rotate_wz;

    gimbal_mode_e gimbal_mode;
} Gimbal_Ctrl_Cmd_s;

// cmd发布的发射控制数据,由shoot订阅
typedef struct
{
    shoot_mode_e shoot_mode;
    loader_mode_e load_mode;
    friction_mode_e friction_mode;
    float bullet_speed; // 实时弹速反馈, unit: m/s
    uint16_t rest_heat;
    float shoot_rate; // 连续发射的射频,unit per s,发/秒
} Shoot_Ctrl_Cmd_s;

/* ----------------gimbal/shoot/chassis发布的反馈数据----------------*/
/**
 * @brief 由cmd订阅,其他应用也可以根据需要获取.
 *
 */

/* @todo : 对于平衡底盘,需要新增控制模式和控制数据 */
typedef struct
{
    float chassis_wz_imu;        // 底盘IMU的Gyro[2], unit: rad/s

    uint16_t rest_heat;           // 剩余枪口热量
    float bullet_speed;          // 实时弹速
    Detect_Color_e self_color;   // 0 for blue, 1 for red

} Chassis_Upload_Data_s;

/* @todo : 对于平衡底盘,需要不同的反馈数据 */
typedef struct
{
    attitude_t gimbal_imu_data;
    uint16_t yaw_motor_single_round_angle;
} Gimbal_Upload_Data_s;

typedef struct
{
    uint16_t rest_heat;           // 剩余枪口热量
} Shoot_Upload_Data_s;

#pragma pack() // 开启字节对齐,结束前面的#pragma pack(1)

#endif // !ROBOT_DEF_H
