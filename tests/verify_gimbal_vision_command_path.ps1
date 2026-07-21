$ErrorActionPreference = 'Stop'

$workspaceRoot = Split-Path -Parent $PSScriptRoot

function Remove-CComments {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    $result = [System.Text.StringBuilder]::new()
    $state = 'Code'

    for ($index = 0; $index -lt $SourceText.Length; $index++) {
        $character = $SourceText[$index]
        $hasNext = ($index + 1) -lt $SourceText.Length
        $nextCharacter = if ($hasNext) { $SourceText[$index + 1] } else { [char]0 }

        switch ($state) {
            'Code' {
                if ($character -eq [char]47 -and $nextCharacter -eq [char]47) {
                    $state = 'LineComment'
                    $index++
                }
                elseif ($character -eq [char]47 -and $nextCharacter -eq [char]42) {
                    $state = 'BlockComment'
                    $index++
                }
                else {
                    [void]$result.Append($character)
                    if ($character -eq [char]34) {
                        $state = 'StringLiteral'
                    }
                    elseif ($character -eq [char]39) {
                        $state = 'CharacterLiteral'
                    }
                }
            }
            'LineComment' {
                if ($character -eq [char]10 -or $character -eq [char]13) {
                    [void]$result.Append($character)
                    $state = 'Code'
                }
            }
            'BlockComment' {
                if ($character -eq [char]42 -and $nextCharacter -eq [char]47) {
                    [void]$result.Append(' ')
                    $state = 'Code'
                    $index++
                }
                elseif ($character -eq [char]10 -or $character -eq [char]13) {
                    [void]$result.Append($character)
                }
            }
            'StringLiteral' {
                [void]$result.Append($character)
                if ($character -eq [char]92 -and $hasNext) {
                    [void]$result.Append($nextCharacter)
                    $index++
                }
                elseif ($character -eq [char]34) {
                    $state = 'Code'
                }
            }
            'CharacterLiteral' {
                [void]$result.Append($character)
                if ($character -eq [char]92 -and $hasNext) {
                    [void]$result.Append($nextCharacter)
                    $index++
                }
                elseif ($character -eq [char]39) {
                    $state = 'Code'
                }
            }
        }
    }

    return $result.ToString()
}

function Remove-CCodeWhitespace {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    $result = [System.Text.StringBuilder]::new()
    $state = 'Code'

    for ($index = 0; $index -lt $SourceText.Length; $index++) {
        $character = $SourceText[$index]
        $hasNext = ($index + 1) -lt $SourceText.Length
        $nextCharacter = if ($hasNext) { $SourceText[$index + 1] } else { [char]0 }

        if ($state -eq 'Code') {
            if ([char]::IsWhiteSpace($character)) {
                continue
            }

            [void]$result.Append($character)
            if ($character -eq [char]34) {
                $state = 'StringLiteral'
            }
            elseif ($character -eq [char]39) {
                $state = 'CharacterLiteral'
            }
            continue
        }

        [void]$result.Append($character)
        if ($character -eq [char]92 -and $hasNext) {
            [void]$result.Append($nextCharacter)
            $index++
        }
        elseif ($state -eq 'StringLiteral' -and $character -eq [char]34) {
            $state = 'Code'
        }
        elseif ($state -eq 'CharacterLiteral' -and $character -eq [char]39) {
            $state = 'Code'
        }
    }

    return $result.ToString()
}

function ConvertTo-NormalizedActiveCCode {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$SourceText)

    $activeCode = Remove-CComments -SourceText $SourceText
    return Remove-CCodeWhitespace -SourceText $activeCode
}

function Test-ActiveCCodeContains {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string]$SourceText,
        [Parameter(Mandatory)][string]$CodeText
    )

    $normalizedSource = ConvertTo-NormalizedActiveCCode -SourceText $SourceText
    $normalizedEntry = ConvertTo-NormalizedActiveCCode -SourceText $CodeText
    if ([string]::IsNullOrEmpty($normalizedEntry)) {
        throw "Contract entry contains no active C code: $CodeText"
    }

    return $normalizedSource.Contains($normalizedEntry)
}

function Assert-ActiveCCodeState {
    param(
        [Parameter(Mandatory)][string]$RelativePath,
        [Parameter(Mandatory)][string]$CodeText,
        [Parameter(Mandatory)][bool]$ShouldBeActive
    )

    $sourcePath = Join-Path $workspaceRoot $RelativePath
    $sourceText = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
    $isActive = Test-ActiveCCodeContains -SourceText $sourceText -CodeText $CodeText

    if ($ShouldBeActive -and -not $isActive) {
        throw "Missing required active code in ${RelativePath}: $CodeText"
    }
    if (-not $ShouldBeActive -and $isActive) {
        throw "Unexpected active code in ${RelativePath}: $CodeText"
    }
}

$commentedRequiredProbe = @'
/* RobotCMDInit(); */
// GimbalInit();
'@
if (Test-ActiveCCodeContains -SourceText $commentedRequiredProbe -CodeText 'RobotCMDInit();') {
    throw 'Self-test failed: a block-commented required entry was treated as active code.'
}
if (Test-ActiveCCodeContains -SourceText $commentedRequiredProbe -CodeText 'GimbalInit();') {
    throw 'Self-test failed: a line-commented required entry was treated as active code.'
}

if (-not (Test-ActiveCCodeContains -SourceText 'ShootInit ();' -CodeText 'ShootInit();')) {
    throw 'Self-test failed: whitespace variation bypassed disabled-entry detection.'
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
    Assert-ActiveCCodeState -RelativePath $disabledLine[0] -CodeText $disabledLine[1] -ShouldBeActive $false
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
    Assert-ActiveCCodeState -RelativePath $requiredEntry[0] -CodeText $requiredEntry[1] -ShouldBeActive $true
}

Write-Output 'Gimbal/vision command-path contract passed.'
