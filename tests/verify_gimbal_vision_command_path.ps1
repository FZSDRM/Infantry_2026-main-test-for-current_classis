$ErrorActionPreference = 'Stop'

$workspaceRoot = Split-Path -Parent $PSScriptRoot

function Get-SourceLines {
    param([Parameter(Mandatory)][string]$RelativePath)

    $sourcePath = Join-Path $workspaceRoot $RelativePath
    return @(Get-Content -LiteralPath $sourcePath -Encoding UTF8)
}

function Assert-NoActiveExactLine {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$LineText
    )

    $activeLines = @(Get-SourceLines -RelativePath $RelativePath | Where-Object { $_.Trim() -eq $LineText })
    if ($activeLines.Count -gt 0) {
        throw "Unexpected active line in ${RelativePath}: $LineText"
    }
}

function Assert-ContainsText {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Text
    )

    $content = Get-Content -LiteralPath (Join-Path $workspaceRoot $RelativePath) -Raw -Encoding UTF8
    if (-not $content.Contains($Text)) {
        throw "Missing required text in ${RelativePath}: $Text"
    }
}

$disabledLines = @(
    @('application/robot.c', 'ShootInit();'),
    @('application/robot.c', 'ShootTask();'),
    @('application/cmd/robot_cmd.c', '#include "can_comm.h"'),
    @('application/cmd/robot_cmd.c', 'shoot_cmd_pub = PubRegister("shoot_cmd", sizeof(Shoot_Ctrl_Cmd_s));'),
    @('application/cmd/robot_cmd.c', 'shoot_feed_sub = SubRegister("shoot_feed", sizeof(Shoot_Upload_Data_s));'),
    @('application/cmd/robot_cmd.c', 'cmd_can_comm = CANCommInit(&comm_conf);'),
    @('application/cmd/robot_cmd.c', 'chassis_fetch_data = *(Chassis_Upload_Data_s *)CANCommGet(cmd_can_comm);'),
    @('application/cmd/robot_cmd.c', 'SubGetMessage(shoot_feed_sub, &shoot_fetch_data);'),
    @('application/cmd/robot_cmd.c', 'CANCommSend(cmd_can_comm, (void *)&chassis_cmd_send);'),
    @('application/cmd/robot_cmd.c', 'PubPushMessage(shoot_cmd_pub, (void *)&shoot_cmd_send);')
)

foreach ($disabledLine in $disabledLines) {
    Assert-NoActiveExactLine -RelativePath $disabledLine[0] -LineText $disabledLine[1]
}

$requiredText = @(
    @('application/robot.c', 'RobotCMDInit();'),
    @('application/robot.c', 'GimbalInit();'),
    @('application/robot.c', 'RobotCMDTask();'),
    @('application/robot.c', 'GimbalTask();'),
    @('application/cmd/robot_cmd.c', 'RemoteControlInit(&huart3)'),
    @('application/cmd/robot_cmd.c', 'TransferImageInit(&huart6)'),
    @('application/cmd/robot_cmd.c', 'VisionInit(&huart1)'),
    @('application/cmd/robot_cmd.c', 'RemoteControlSet();'),
    @('application/cmd/robot_cmd.c', 'ImageMouseKeySet();'),
    @('application/cmd/robot_cmd.c', 'gimbal_cmd_send.gimbal_mode = GIMBAL_ZERO_FORCE;'),
    @('application/cmd/robot_cmd.c', 'PubPushMessage(gimbal_cmd_pub, (void *)&gimbal_cmd_send);')
)

foreach ($requiredEntry in $requiredText) {
    Assert-ContainsText -RelativePath $requiredEntry[0] -Text $requiredEntry[1]
}

Write-Output 'Gimbal/vision command-path contract passed.'
