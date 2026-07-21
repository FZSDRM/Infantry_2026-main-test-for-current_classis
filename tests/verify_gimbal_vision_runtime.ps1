$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'verify_gimbal_vision_command_path.ps1')

$disabledCalls = @(
    [PSCustomObject]@{ Path = 'modules/motor/motor_task.c'; Body = 'MotorControlTask'; Call = 'LKMotorControl' }
    [PSCustomObject]@{ Path = 'modules/motor/motor_task.c'; Body = 'MotorControlTask'; Call = 'HTMotorControl' }
    [PSCustomObject]@{ Path = 'modules/motor/motor_task.c'; Body = 'MotorControlTask'; Call = 'ServeoMotorControl' }
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Body = 'BSPInit'; Call = 'LEDInit' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Body = 'GimbalTask'; Call = 'VOFA' }
)

foreach ($entry in $disabledCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -BodyFunction $entry.Body -FunctionName $entry.Call -ShouldBePresent $false
}

$disabledIdentifiers = @(
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Identifier = 'chassis_speed_sub' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Identifier = 'VOFA' }
)

foreach ($entry in $disabledIdentifiers) {
    Assert-CIdentifierState -RelativePath $entry.Path -Identifier $entry.Identifier -ShouldBePresent $false
}

$disabledTokenSequences = @(
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Code = '#include "bsp_led.h"' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '#include "vofa.h"' }
)

foreach ($entry in $disabledTokenSequences) {
    Assert-CTokenSequenceState -RelativePath $entry.Path -CodeText $entry.Code -ShouldBePresent $false
}

Assert-CBodyTokenSequenceState -RelativePath 'Src/freertos.c' -BodyFunction 'MX_FREERTOS_Init' `
    -CodeText 'uiTaskHandle = osThreadCreate(osThread(uitask), NULL);' -ShouldBePresent $false

$requiredBodyTokenSequences = @(
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'MX_FREERTOS_Init'; Code = 'defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'MX_FREERTOS_Init'; Code = 'insTaskHandle = osThreadCreate(osThread(instask), NULL);' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'MX_FREERTOS_Init'; Code = 'motorTaskHandle = osThreadCreate(osThread(motortask), NULL);' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'MX_FREERTOS_Init'; Code = 'daemonTaskHandle = osThreadCreate(osThread(daemontask), NULL);' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'MX_FREERTOS_Init'; Code = 'robotTaskHandle = osThreadCreate(osThread(robottask), NULL);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Body = 'GimbalInit'; Code = 'yaw_motor = DJIMotorInit(&yaw_config);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Body = 'GimbalInit'; Code = 'pitch_motor = DJIMotorInit(&pitch_config);' }
)

foreach ($entry in $requiredBodyTokenSequences) {
    Assert-CBodyTokenSequenceState -RelativePath $entry.Path -BodyFunction $entry.Body -CodeText $entry.Code -ShouldBePresent $true
}

$requiredCalls = @(
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'StartINSTASK'; Call = 'VisionSend' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'StartMOTORTASK'; Call = 'MotorControlTask' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'StartDAEMONTASK'; Call = 'DaemonTask' }
    [PSCustomObject]@{ Path = 'Src/freertos.c'; Body = 'StartROBOTTASK'; Call = 'RobotTask' }
    [PSCustomObject]@{ Path = 'modules/motor/motor_task.c'; Body = 'MotorControlTask'; Call = 'DJIMotorControl' }
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Body = 'BSPInit'; Call = 'DWT_Init' }
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Body = 'BSPInit'; Call = 'BSPLogInit' }
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Body = 'BSPInit'; Call = 'IMUTempInit' }
    [PSCustomObject]@{ Path = 'bsp/bsp_init.c'; Body = 'BSPInit'; Call = 'BuzzerInit' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Body = 'GimbalTask'; Call = 'MotorOfflineAlarmTask' }
)

foreach ($entry in $requiredCalls) {
    Assert-CFunctionCallState -RelativePath $entry.Path -BodyFunction $entry.Body -FunctionName $entry.Call -ShouldBePresent $true
}

Assert-CTokenSequenceState -RelativePath 'application/gimbal/gimbal.c' `
    -CodeText 'static Chassis_Upload_Data_s chassis_real_speed;' -ShouldBePresent $true

Write-Output 'Gimbal/vision runtime contract passed.'
