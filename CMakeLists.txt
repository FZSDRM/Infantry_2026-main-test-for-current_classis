cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME basic_framework)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/Third_Party/SEGGER/RTT/SEGGER_RTT_printf.c
    Middlewares/Third_Party/SEGGER/RTT/SEGGER_RTT.c
    Middlewares/Third_Party/SEGGER/RTT/SEGGER_RTT_ASM_ARMv7M.s
    bsp/dwt/bsp_dwt.c
    bsp/pwm/bsp_pwm.c
    bsp/bsp_legacy_support/bsp_temperature.c
    bsp/bsp_legacy_support/bsp_buzzer.c
    bsp/bsp_legacy_support/bsp_led.c
    bsp/gpio/bsp_gpio.c
    bsp/spi/bsp_spi.c
    bsp/iic/bsp_iic.c
    bsp/can/bsp_can.c
    bsp/usart/bsp_usart.c
    bsp/usb/bsp_usb.c
    bsp/log/bsp_log.c
    bsp/bsp_init.c
    bsp/rng/bsp_rng.c
    modules/algorithm/controller.c
    modules/algorithm/kalman_filter.c
    modules/algorithm/simple_kalman.c
    modules/algorithm/QuaternionEKF.c
    modules/algorithm/crc8.c
    modules/algorithm/crc16.c
    modules/algorithm/user_lib.c
    modules/BMI088/bmi088.c
    modules/imu/BMI088driver.c
    modules/imu/BMI088Middleware.c
    modules/imu/ins_task.c
    modules/ist8310/ist8310.c
    modules/led_task/led_task.c
    modules/led/led.c
    modules/master_machine/master_process.c
    modules/master_machine/seasky_protocol.c
    modules/master_machine/serialport_protocol.c
    modules/motor/DJImotor/dji_motor.c
    modules/motor/HTmotor/HT04.c
    modules/motor/LKmotor/LK9025.c
    modules/motor/step_motor/step_motor.c
    modules/motor/servo_motor/servo_motor.c
    modules/motor/motor_task.c
    modules/motor_offline_alarm/motor_offline_alarm.c
    modules/oled/oled.c
    modules/referee/crc_ref.c
    modules/referee/rm_referee.c
    modules/referee/ui_interface.c
    modules/referee/ui_g.c
    modules/referee/referee_task.c
    modules/remote/remote_control.c
    modules/remote/transfer_image.c
    modules/super_cap/super_cap.c
    modules/can_comm/can_comm.c
    modules/message_center/message_center.c
    modules/daemon/daemon.c
    modules/vofa/vofa.c
    application/gimbal/gimbal.c
    application/gimbal/gm6020_id_scan_logic.c
    application/chassis/chassis.c
    application/chassis/chassis_force_ctrl.c
    application/shoot/shoot.c
    application/cmd/robot_cmd.c
    application/balance_chassis/balance.c
    application/robot.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Middlewares/Third_Party/SEGGER/RTT
    Middlewares/Third_Party/SEGGER/Config
    application/chassis
    application/shoot
    application/gimbal
    application/cmd
    application/balance_chassis
    application
    bsp/dwt
    bsp/can
    bsp/usart
    bsp/usb
    bsp/gpio
    bsp/spi
    bsp/iic
    bsp/log
    bsp/pwm
    bsp/rng
    bsp/bsp_legacy_support
    bsp
    modules/algorithm
    modules/BMI088
    modules/imu
    modules/ist8310
    modules/led
    modules/led_task
    modules/master_machine
    modules/motor/DJImotor
    modules/motor/LKmotor
    modules/motor/HTmotor
    modules/motor/step_motor
    modules/motor/servo_motor
    modules/motor
    modules/motor_offline_alarm
    modules/oled
    modules/referee
    modules/remote
    modules/super_cap
    modules/can_comm
    modules/message_center
    modules/daemon
    modules/vofa
    modules
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ARM_MATH_CM4
    ARM_MATH_MATRIX_CHECK
    ARM_MATH_ROUNDING
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
