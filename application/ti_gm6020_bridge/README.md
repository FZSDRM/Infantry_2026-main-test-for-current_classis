# TI 主控到 GM6020 专用执行器从板

该模式把 C 板精简为带本地惯导的摆杆执行器从板：USART1 接收 TI MSPM0G3507 的杆侧目标，C 板执行位置外环、速度内环，最后通过 CAN1 的 GM6020 电流帧驱动一台电机。板载 BMI088 保持 1 kHz 解算并提供去重力底盘加速度；原云台、底盘、遥控器、旧视觉和裁判系统仍保留在仓库中，但 `TI_GM6020_BRIDGE_MODE=1` 时不会初始化或创建对应任务。

## 接线与串口

- TI `PB0/UART0_TX` → C 板 `PB7/USART1_RX`；
- TI `PB1/UART0_RX` ← C 板 `PA9/USART1_TX`；
- 两板 GND 必须共地，电平均为 3.3 V TTL；
- 通信参数固定为 115200、8 数据位、无校验、1 停止位；
- GM6020 接 C 板 CAN1，默认拨码 ID 为 1。

C 板启动时会把 CubeMX 原有的 USART1 波特率运行时改为 115200，不修改生成文件。UART RX 使用 DMA+IDLE，协议解析器可以跨任意 DMA 分块重新拼帧；UART TX 使用 DMA 回传状态。

## 运行任务

桥接模式只保留四个常驻业务任务：

- `InsTask`，1 kHz：读取板载 BMI088，完成姿态解算、重力分离和加热控制；
- `RobotTask`，500 Hz：消费最新串口命令、更新 GM6020 参考、执行 50 ms 命令失联保护，并以 50 Hz 回传状态；
- `MotorTask`，500 Hz：运行 GM6020 位置/速度串级 PID，打包 CAN1 电流帧；
- `DaemonTask`，100 Hz：检查 GM6020 CAN 反馈，约 200 ms 没有反馈后标记离线。

CubeMX 生成的 `defaultTask` 仍按生成流程启动，执行 USB 初始化后立即自删除，不进入控制链。恢复原机器人应用时将 `application/robot_def.h` 中 `TI_GM6020_BRIDGE_MODE` 改为 0，并重新检查原任务装配。

## 协议

帧头为 `A5 5A`，随后依次为版本、消息类型、载荷长度、16 位小端序号、载荷和 CRC。CRC 使用 CRC-16/CCITT-FALSE，初值 `0xFFFF`、多项式 `0x1021`，CRC 本身按小端发送。

- `0x01`：TI 下发控制命令；位置、速度和 G354 杆角均使用 `µrad` 定点数；
- `0x81`：C 板回传电机在线、使能、故障、标定状态，以及多圈位置、估算杆角、速度、电流和温度；
- TI 正常约 200 Hz 下发 25 字节命令，C 板 50 Hz 回传 29 字节状态，总占用低于 115200 8N1 的有效带宽。

无 G354 的 PB21 台架模式会在首次使能时把 GM6020 当前多圈位置定义为相对零点；正式滚球模式必须携带有效 G354 杆角，C 板据此建立电机零偏。停止、命令超时或 CAN 离线都会清除该标定，下一次使能重新对零。

## 控制与安全参数

所有 C 板参数集中在 `application/robot_def.h` 的 `TI_GM6020_BRIDGE_*` 宏：

- `MOTOR_ID`、`MOTOR_DIRECTION_SIGN`、`TRANSMISSION_RATIO` 决定 CAN 地址和机械映射；
- `ROD_MIN_RAD`、`ROD_MAX_RAD`、`ROD_MAX_SPEED_RPS` 是独立于 TI 的第二道机械保护；
- `ANGLE_KP/KI/KD` 输出电机速度参考，单位链为“度 → 度每秒”；
- `SPEED_KP/KI/KD` 输出 GM6020 电流原始值；
- `CURRENT_MAX_RAW` 必须先保守设置，架空确认方向后再逐步提高。
- `CHASSIS_ACCEL_AXIS/SIGN` 把 BMI088 `MotionAccel_b` 映射到摆杆正方向；默认沿用既有底盘约定 `-X` 为车头正向；
- `IMU_TIMEOUT_MS` 和 `CHASSIS_ACCEL_MAX_MPS2` 分别限制样本停更时间和可交给本地平衡算法的加速度边界。

Live Watch 可查看 `gTiGm6020BridgeImuOnline`、`gTiGm6020BridgeImuSampleCount` 和 `gTiGm6020BridgeChassisAccelerationMps2`。前两项用于确认 1 kHz 解算持续运行，后一项是后续 C 板本地滚球控制器应直接使用的底盘加速度输入；当前执行器桥接仍按 TI 下发的杆目标工作，不会在尚未标定增益时把该加速度直接叠加到 GM6020 电流。

首次上电不要直接安装负载调 PID。先将机构架空，确认 CAN ID、正负方向、相对零点和角度保护，再从低电流、低速度开始调速度内环，最后调位置外环。当前修改只完成源码、协议和 ARM 构建验证，尚未进行带电硬件验证。
