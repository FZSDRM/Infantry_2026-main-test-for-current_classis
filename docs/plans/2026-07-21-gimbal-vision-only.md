# Gimbal Vision Only Implementation Plan

> **For implementer:** Use TDD throughout. Write failing test first. Watch it fail. Then implement.

**Goal:** Create a dedicated branch that runs only yaw/pitch gimbal control, vision communication, and the existing remote/vision switching and safety logic.

**Architecture:** Keep the existing `GIMBAL_BOARD` control path and deactivate unrelated runtime entry points with comments. Do not rewrite `RobotCMD`, alter CubeMX-generated peripheral initialization, or remove sources from the Makefile; preserve the existing control-source selection while disconnecting shoot, chassis CAN, UI, and non-DJI motor execution.

**Tech Stack:** STM32F407 HAL, FreeRTOS/CMSIS-RTOS, Arm GNU Toolchain, GNU Make, PowerShell 7 static contract tests.

---

### Task 1: Isolate the gimbal/vision command path

**Files:**

- Create: `tests/verify_gimbal_vision_command_path.ps1`
- Modify: `application/robot.c`
- Modify: `application/cmd/robot_cmd.c`

**Step 1: Write the failing test**

Create `tests/verify_gimbal_vision_command_path.ps1` with:

```powershell
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
```

**Step 2: Run test — confirm it fails**

Command:

```powershell
$ErrorActionPreference = 'Stop'
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
& $pwshPath -NoLogo -NoProfile -NonInteractive -File '.\tests\verify_gimbal_vision_command_path.ps1'
if ($LASTEXITCODE -eq 0) { throw 'Expected the command-path contract to fail before implementation.' }
```

Expected: non-zero exit with the first still-active shoot or chassis-CAN line.

**Step 3: Write minimal implementation**

In `application/robot.c`, comment the shoot include and runtime calls while retaining `RobotCMD` and gimbal calls:

```c
#ifdef GIMBAL_BOARD
#include "gimbal.h"
// #include "shoot.h" // 云台视觉专用分支不启用发射机构
#endif
```

```c
#ifdef GIMBAL_BOARD
    GimbalInit();
    // ShootInit(); // 云台视觉专用分支不启用发射机构
#endif
```

```c
#ifdef GIMBAL_BOARD
    GimbalTask();
    // ShootTask(); // 云台视觉专用分支不启用发射机构
#endif
```

In `application/cmd/robot_cmd.c`, comment the `can_comm.h` include, `cmd_can_comm` declaration, shoot publisher/subscriber/feedback declarations, shoot message registration, the complete `CANComm_Init_Config_s` initialization block, chassis CAN receive, shoot feedback receive, chassis CAN send, and shoot command publish. Retain `shoot_cmd_send` only as internal state used by the unchanged switching logic. Add `// 云台视觉专用分支：...` on every deactivated call.

**Step 4: Run test — confirm it passes**

Command:

```powershell
$ErrorActionPreference = 'Stop'
$pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
& $pwshPath -NoLogo -NoProfile -NonInteractive -File '.\tests\verify_gimbal_vision_command_path.ps1'
if ($LASTEXITCODE -ne 0) { throw "Command-path contract failed with exit code $LASTEXITCODE" }
```

Expected: `Gimbal/vision command-path contract passed.`

**Step 5: Commit**

Stage only the test and two implementation files, then commit with intent and OMC trailers. Do not stage `.gitignore`.

---

### Task 2: Disable unrelated periodic work

**Files:**

- Create: `tests/verify_gimbal_vision_runtime.ps1`
- Modify: `Src/freertos.c`
- Modify: `modules/motor/motor_task.c`
- Modify: `bsp/bsp_init.c`
- Modify: `application/gimbal/gimbal.c`

**Step 1: Write the failing test**

Create `tests/verify_gimbal_vision_runtime.ps1` with the same `Get-SourceLines`, `Assert-NoActiveExactLine`, and `Assert-ContainsText` helpers from Task 1, followed by:

```powershell
$disabledLines = @(
    @('Src/freertos.c', 'uiTaskHandle = osThreadCreate(osThread(uitask), NULL);'),
    @('modules/motor/motor_task.c', 'LKMotorControl();'),
    @('modules/motor/motor_task.c', 'HTMotorControl();'),
    @('modules/motor/motor_task.c', 'ServeoMotorControl();'),
    @('bsp/bsp_init.c', 'LEDInit();'),
    @('application/gimbal/gimbal.c', 'chassis_speed_sub = SubRegister("chassis_feed", sizeof(Chassis_Upload_Data_s));'),
    @('application/gimbal/gimbal.c', 'SubGetMessage(chassis_speed_sub, &chassis_real_speed);'),
    @('application/gimbal/gimbal.c', 'VOFA(0, gimba_IMU_data->YawTotalAngle, chassis_real_speed.chassis_wz_imu, chassis_wz_acc_filtered, yaw_spin_current_feedforward, (float)yaw_motor->measure.real_current, yaw_current_feedforward);')
)

foreach ($disabledLine in $disabledLines) {
    Assert-NoActiveExactLine -RelativePath $disabledLine[0] -LineText $disabledLine[1]
}

$requiredText = @(
    @('Src/freertos.c', 'defaultTaskHandle = osThreadCreate'),
    @('Src/freertos.c', 'insTaskHandle = osThreadCreate'),
    @('Src/freertos.c', 'motorTaskHandle = osThreadCreate'),
    @('Src/freertos.c', 'daemonTaskHandle = osThreadCreate'),
    @('Src/freertos.c', 'robotTaskHandle = osThreadCreate'),
    @('Src/freertos.c', 'VisionSend();'),
    @('Src/freertos.c', 'MotorControlTask();'),
    @('Src/freertos.c', 'DaemonTask();'),
    @('Src/freertos.c', 'RobotTask();'),
    @('modules/motor/motor_task.c', 'DJIMotorControl();'),
    @('bsp/bsp_init.c', 'DWT_Init(168);'),
    @('bsp/bsp_init.c', 'IMUTempInit();'),
    @('bsp/bsp_init.c', 'BuzzerInit();'),
    @('application/gimbal/gimbal.c', 'yaw_motor = DJIMotorInit(&yaw_config);'),
    @('application/gimbal/gimbal.c', 'pitch_motor = DJIMotorInit(&pitch_config);'),
    @('application/gimbal/gimbal.c', 'MotorOfflineAlarmTask(gimbal_offline_alarm);')
)

foreach ($requiredEntry in $requiredText) {
    Assert-ContainsText -RelativePath $requiredEntry[0] -Text $requiredEntry[1]
}

Write-Output 'Gimbal/vision runtime contract passed.'
```

**Step 2: Run test — confirm it fails**

Run the script with the PowerShell 7 command pattern from Task 1.

Expected: non-zero exit because UI, other motor families, LED, chassis feedback, or VOFA is still active.

**Step 3: Write minimal implementation**

- In `Src/freertos.c`, comment the UI task creation call only; leave its unreachable generated function in place.
- In `modules/motor/motor_task.c`, comment `LKMotorControl()`, `HTMotorControl()`, and `ServeoMotorControl()`; keep `DJIMotorControl()`.
- In `bsp/bsp_init.c`, comment the LED include and `LEDInit()`; retain DWT, log, IMU temperature control, and buzzer initialization.
- In `application/gimbal/gimbal.c`, comment the VOFA include/call and chassis-feed subscriber declaration, registration, and receive call. Keep the zero-initialized chassis feedback structure because existing feedforward expressions reference it and evaluate to no chassis compensation.

**Step 4: Run both static tests — confirm they pass**

Run both test scripts in separate PowerShell 7 invocations and explicitly check each `$LASTEXITCODE`.

Expected: both contract pass messages.

**Step 5: Commit**

Stage only the runtime test and four implementation files, then commit with intent and OMC trailers. Do not stage `.gitignore`.

---

### Task 3: Build and audit the dedicated branch

**Files:**

- Verify: `Makefile`
- Verify: all files modified in Tasks 1–2

**Step 1: Run the complete firmware build**

Command:

```powershell
$ErrorActionPreference = 'Stop'
make -j4
if ($LASTEXITCODE -ne 0) { throw "Firmware build failed with exit code $LASTEXITCODE" }
```

Expected: `build/basic_framework.elf`, `.hex`, and `.bin` are produced, and `arm-none-eabi-size` reports the image size.

**Step 2: Re-run both static tests**

Expected: both pass.

**Step 3: Audit changes**

Run `git diff --check`, inspect `git diff master...HEAD`, and inspect `git status --short --branch`. Confirm:

- the current branch is `feature/gimbal-vision-only`;
- only the approved entry points were deactivated;
- remote/vision switching and zero-force behavior remain present;
- `.gitignore` remains the user's separate modification and was not committed;
- build artifacts remain ignored or uncommitted.

**Step 4: Report hardware validation boundary**

Report source/static/build success separately from physical-board validation. Do not flash without a separate request.
