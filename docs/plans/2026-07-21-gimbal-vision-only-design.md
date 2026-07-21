# 云台视觉专用分支设计

## 目标

在独立分支 `feature/gimbal-vision-only` 中，仅运行云台 yaw/pitch 电机、云台 IMU、视觉上位机通信，以及原有遥控器/图传遥控器切换和安全保护逻辑。底盘、发射、裁判 UI 和其他类型电机不再产生运行时动作。

## 方案选择

采用最小裁剪：保留现有模块和编译结构，只在应用初始化、RTOS 任务入口和周期控制入口注释不需要的调用。这样不会复制或重写 `RobotCMD` 的切换逻辑，也不会改动 CubeMX 生成的外设初始化代码。

未采用以下方案：

- 全局模式宏：专用分支不需要在同一份源码中反复切换模式，额外宏会增加条件编译复杂度。
- 新建精简版 `RobotCMD`：会复制现有遥控器、图传和视觉切换逻辑，后续容易与主线行为不一致。

## 运行架构

保留的运行链路如下：

1. 默认任务初始化 USB VCP，供视觉上位机通信。
2. INS 任务更新云台姿态，并周期发送视觉姿态数据。
3. Robot 任务运行原有 `RobotCMDTask()` 和 `GimbalTask()`。
4. Motor 任务仅运行 DJI 电机控制，因此只向已注册的 yaw/pitch GM6020 输出。
5. Daemon 任务保留遥控器、视觉和电机离线检测。

## 组件裁剪

### `application/robot.c`

- 保留 `BSPInit()`、`RobotCMDInit()`、`GimbalInit()`。
- 注释 `ShootInit()` 和 `ShootTask()`。
- 当前 `GIMBAL_BOARD` 配置本身不会启动底盘或平衡底盘应用。

### `application/cmd/robot_cmd.c`

- 保留 DJI 遥控器、图传遥控器、视觉初始化和全部原切换逻辑。
- 保留 gimbal 命令发布和反馈订阅。
- 注释 shoot 消息注册、收取和发布。
- 注释云台板与底盘板之间的 CAN 通信初始化、收取和发送，避免专用分支产生底盘控制报文。
- shoot/chassis 命令结构可继续作为切换逻辑内部状态使用，但不连接任何实际执行模块。

### `Src/freertos.c`

- 保留 default/INS/motor/daemon/robot 任务。
- 注释 UI 任务创建，因此裁判 UI 不运行。

### `modules/motor/motor_task.c`

- 保留 `DJIMotorControl()`。
- 注释 LK、HT 和舵机周期控制调用。

### `bsp/bsp_init.c`

- 注释 LED 初始化。
- 保留 DWT、日志、IMU 温控和蜂鸣器；它们分别被时间基准、诊断、IMU 和电机离线报警依赖。

### 生成代码与外设初始化

不修改 CubeMX 生成的 `Src/main.c` 外设初始化列表，也不删除 Makefile 源文件。未注册的应用和驱动不会执行实际控制，链接器会裁剪不可达代码；保留生成配置可降低误删 IMU、CAN2、USB、DMA、遥控 UART 或蜂鸣器定时器依赖的风险。

## 数据流

遥控器或图传遥控器决定当前控制模式；视觉模式仍按现有条件接管目标角。`RobotCMDTask()` 将最终 yaw/pitch 目标发布到 `gimbal_cmd`，`GimbalTask()` 结合 IMU 反馈计算控制量，`DJIMotorControl()` 通过 CAN2 驱动两个 GM6020。云台姿态经视觉模块和 USB VCP 返回上位机。

## 安全与异常处理

- 两种遥控器均离线时，沿用现有逻辑将云台设为 `GIMBAL_ZERO_FORCE`。
- 视觉无目标或离线时，沿用现有手动控制/无目标处理。
- yaw/pitch 电机离线检测与蜂鸣器报警继续运行。
- 不引入新的自动使能路径，不改变拨杆、按键或图传切换语义。

## 验证策略

1. 先增加 PowerShell 静态测试，确认不需要的运行入口仍处于激活状态，并观察测试失败。
2. 完成最小注释后重新运行静态测试，确认只保留目标链路。
3. 使用现有 Arm GNU Makefile 完整编译。
4. 检查 Git diff 和工作区状态，确保用户已有 `.gitignore` 修改未进入本功能提交。
5. 不执行烧录；硬件行为验证留给实际云台板测试。
