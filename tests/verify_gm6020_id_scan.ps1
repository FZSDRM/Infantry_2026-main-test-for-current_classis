$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'verify_gimbal_vision_command_path.ps1')

function Assert-TextMatch {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][string]$Description
    )

    $fullPath = Join-Path $workspaceRoot $RelativePath
    $source = Get-Content -LiteralPath $fullPath -Raw -Encoding UTF8
    if ($source -notmatch $Pattern) {
        throw "Missing required structure in ${RelativePath}: $Description"
    }
}

function Get-CTokenSequenceIndex {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Tokens,
        [Parameter(Mandatory)][string]$CodeText
    )

    $patternTokens = @(Get-ActiveCTokens -SourceText $CodeText)
    for ($startIndex = 0; $startIndex -le $Tokens.Count - $patternTokens.Count; $startIndex++) {
        $matchesSequence = $true
        for ($patternIndex = 0; $patternIndex -lt $patternTokens.Count; $patternIndex++) {
            $sourceToken = $Tokens[$startIndex + $patternIndex]
            $patternToken = $patternTokens[$patternIndex]
            if ($sourceToken.Kind -cne $patternToken.Kind -or
                $sourceToken.Text -cne $patternToken.Text -or
                $sourceToken.IsPreprocessorDirective -ne $patternToken.IsPreprocessorDirective) {
                $matchesSequence = $false
                break
            }
        }
        if ($matchesSequence) {
            return $startIndex
        }
    }

    return -1
}

$requiredTokenSequences = @(
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_MODE' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_FIRST_ID 1u' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_LAST_ID 7u' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_MAX_SPEED_DPS 10.0f' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_TRAVEL_DEG 10.0f' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_ORIGIN_TOL_DEG 0.5f' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_ONLINE_TIMEOUT_MS 1000u' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_MOTION_TIMEOUT_MS 3000u' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_SETTLE_MS 300u' }
    [PSCustomObject]@{ Path = 'application/robot_def.h'; Code = '#define GM6020_ID_SCAN_CURRENT_MAX_RAW 3000.0f' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '#include "gm6020_id_scan_logic.h"' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'static DJIMotorInstance *scan_motors[GM6020_ID_SCAN_MOTOR_COUNT];' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.can_handle = &hcan2' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.motor_type = GM6020_CURRENT' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.angle_feedback_source = MOTOR_FEED' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.speed_feedback_source = MOTOR_FEED' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.outer_loop_type = SPEED_LOOP' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.close_loop_type = SPEED_LOOP' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.feedforward_flag = FEEDFORWARD_NONE' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = '.MaxOut = GM6020_ID_SCAN_CURRENT_MAX_RAW' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'DJIMotorSetRef(scan_motors[motor_index], 0.0f);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'DJIMotorStop(scan_motors[motor_index]);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'gimba_IMU_data = INS_Init();' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'gimbal_pub = PubRegister("gimbal_feed", sizeof(Gimbal_Upload_Data_s));' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'gimbal_sub = SubRegister("gimbal_cmd", sizeof(Gimbal_Ctrl_Cmd_s));' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'input.enabled = gimbal_cmd_recv.gimbal_mode != GIMBAL_ZERO_FORCE;' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'input.online = DJIMotorHasFeedback(scan_motors[selected_motor]) && DJIMotorIsOnline(scan_motors[selected_motor]);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'input.angle_deg = scan_motors[selected_motor]->measure.total_angle;' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'case GM6020_SCAN_ACTION_STOP_ALL:' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'case GM6020_SCAN_ACTION_STOP_CURRENT:' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'case GM6020_SCAN_ACTION_RUN_CURRENT:' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'speed_ref = ClampScanSpeed(output.speed_ref_dps);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'DJIMotorSetRef(scan_motors[output.motor_index], speed_ref);' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'gimbal_feedback_data.gimbal_imu_data = *gimba_IMU_data;' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'gimbal_feedback_data.yaw_motor_single_round_angle = scan_motors[0]->measure.angle_single_round;' }
    [PSCustomObject]@{ Path = 'application/gimbal/gimbal.c'; Code = 'PubPushMessage(gimbal_pub, (void *)&gimbal_feedback_data);' }
    [PSCustomObject]@{ Path = 'modules/motor/DJImotor/dji_motor.h'; Code = 'uint8_t feedback_received;' }
    [PSCustomObject]@{ Path = 'modules/motor/DJImotor/dji_motor.h'; Code = 'uint8_t DJIMotorHasFeedback(void *motor);' }
    [PSCustomObject]@{ Path = 'modules/motor/DJImotor/dji_motor.c'; Code = 'memset(sender_assignment[group].tx_buff + 2 * num, 0, 2u);' }
    [PSCustomObject]@{ Path = 'modules/motor/DJImotor/dji_motor.c'; Code = 'memset(instance, 0, sizeof(DJIMotorInstance));' }
)

foreach ($entry in $requiredTokenSequences) {
    Assert-CTokenSequenceState -RelativePath $entry.Path -CodeText $entry.Code -ShouldBePresent $true
}

Assert-CTokenSequenceState -RelativePath 'modules/motor/DJImotor/dji_motor.c' `
    -CodeText 'memset(sender_assignment[group].tx_buff + 2 * num, 0, 16u);' -ShouldBePresent $false
Assert-CTokenSequenceState -RelativePath 'application/gimbal/gimbal.c' `
    -CodeText 'input.online = DJIMotorIsOnline(scan_motors[selected_motor]);' -ShouldBePresent $false
Assert-CBodyTokenSequenceState -RelativePath 'modules/motor/DJImotor/dji_motor.c' -BodyFunction 'DecodeDJIMotor' `
    -CodeText 'motor->feedback_received = 1u;' -ShouldBePresent $true
Assert-CBodyTokenSequenceState -RelativePath 'modules/motor/DJImotor/dji_motor.c' -BodyFunction 'DJIMotorHasFeedback' `
    -CodeText 'if (m == NULL) return 0u;' -ShouldBePresent $true
Assert-CBodyTokenSequenceState -RelativePath 'modules/motor/DJImotor/dji_motor.c' -BodyFunction 'DJIMotorHasFeedback' `
    -CodeText 'return m->feedback_received;' -ShouldBePresent $true

$decodeTokens = @(Get-SourceFunctionBodyTokens -RelativePath 'modules/motor/DJImotor/dji_motor.c' `
    -FunctionName 'DecodeDJIMotor' | Where-Object { -not $_.IsPreprocessorDirective })
$decodeGuardIndex = Get-CTokenSequenceIndex -Tokens $decodeTokens `
    -CodeText 'if (_instance == NULL || _instance->rx_len != 8u) return;'
$rxBufferReadIndex = Get-CTokenSequenceIndex -Tokens $decodeTokens -CodeText '_instance->rx_buff'
$daemonReloadIndex = Get-CTokenSequenceIndex -Tokens $decodeTokens -CodeText 'DaemonReload(motor->motor_daemon);'
$feedbackLatchIndex = Get-CTokenSequenceIndex -Tokens $decodeTokens -CodeText 'motor->feedback_received = 1u;'
if ($decodeGuardIndex -ne 0 -or
    $rxBufferReadIndex -le $decodeGuardIndex -or
    $daemonReloadIndex -le $decodeGuardIndex -or
    $feedbackLatchIndex -le $decodeGuardIndex) {
    throw 'DecodeDJIMotor must reject NULL/non-8-byte frames before reading rx_buff, reloading the daemon, or latching first feedback.'
}

$djiMotorSource = Get-Content -LiteralPath (Join-Path $workspaceRoot 'modules/motor/DJImotor/dji_motor.c') -Raw -Encoding UTF8
$feedbackSetMatches = @([regex]::Matches($djiMotorSource, 'feedback_received\s*=\s*1u\s*;'))
if ($feedbackSetMatches.Count -ne 1) {
    throw "feedback_received must be set exactly once, inside DecodeDJIMotor; found $($feedbackSetMatches.Count) assignments."
}

Assert-TextMatch -RelativePath 'application/gimbal/gimbal.c' `
    -Pattern '(?s)#ifdef\s+GM6020_ID_SCAN_MODE.*?void\s+GimbalInit\s*\([^)]*\).*?void\s+GimbalTask\s*\([^)]*\).*?#else.*?void\s+GimbalInit\s*\([^)]*\).*?void\s+GimbalTask\s*\([^)]*\).*?#endif' `
    -Description 'GM6020 scan GimbalInit/GimbalTask branch and original yaw/pitch #else branch'
Assert-TextMatch -RelativePath 'application/gimbal/gimbal.c' `
    -Pattern '(?s)for\s*\(\s*uint8_t\s+motor_index\s*=\s*0u\s*;\s*motor_index\s*<\s*GM6020_ID_SCAN_MOTOR_COUNT\s*;\s*motor_index\+\+\s*\).*?tx_id\s*=\s*GM6020_ID_SCAN_FIRST_ID\s*\+\s*motor_index.*?DJIMotorInit' `
    -Description 'CAN2 GM6020 registration loop covering configured IDs 1 through 7'
Assert-TextMatch -RelativePath 'application/gimbal/gimbal.c' `
    -Pattern '(?s)GM6020_SCAN_ACTION_RUN_CURRENT.*?StopAllScanMotors\s*\(\s*\).*?DJIMotorEnable\s*\(\s*scan_motors\s*\[\s*output\.motor_index\s*\]\s*\)' `
    -Description 'run action stops every motor before enabling only the selected motor'
Assert-TextMatch -RelativePath 'Makefile' -Pattern '(?m)^application/gimbal/gm6020_id_scan_logic\.c\s*\\\s*$' `
    -Description 'pure GM6020 scan logic source in C_SOURCES'

& git diff --quiet HEAD -- 'application/robot.c'
if ($LASTEXITCODE -ne 0) {
    throw 'application/robot.c changed even though the existing GimbalInit/GimbalTask route must be reused.'
}

Write-Output 'GM6020 ID scan integration contract passed.'
