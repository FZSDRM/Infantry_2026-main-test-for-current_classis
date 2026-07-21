#include "gm6020_id_scan_logic.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                       \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    } while (0)

static GM6020IDScanLogicConfig DefaultConfig(void)
{
    const GM6020IDScanLogicConfig config = {
        .motor_count = 7u,
        .max_speed_dps = 10.0f,
        .travel_deg = 10.0f,
        .origin_tolerance_deg = 0.5f,
        .online_timeout_ms = 1000u,
        .motion_timeout_ms = 3000u,
        .settle_ms = 300u,
    };
    return config;
}

static GM6020IDScanOutput Step(GM6020IDScanLogic *context,
                              bool enabled,
                              bool online,
                              uint32_t now_ms,
                              float angle_deg)
{
    const GM6020IDScanInput input = {
        .enabled = enabled,
        .online = online,
        .now_ms = now_ms,
        .angle_deg = angle_deg,
    };
    return GM6020IDScanLogicStep(context, &input);
}

static void ArmAndStartForward(GM6020IDScanLogic *context,
                               uint32_t arm_ms,
                               uint32_t online_ms,
                               float start_angle_deg)
{
    GM6020IDScanOutput output = Step(context, true, false, arm_ms, 0.0f);
    ASSERT_TRUE(context->state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(output.action != GM6020_SCAN_ACTION_RUN_CURRENT);

    output = Step(context, true, true, online_ms, start_angle_deg);
    ASSERT_TRUE(context->state == GM6020_SCAN_FORWARD);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_RUN_CURRENT);
}

static void TestDisabledResetsToDisarmed(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 10u, 20u, 25.0f);

    const GM6020IDScanOutput output = Step(&context, false, true, 30u, 27.0f);

    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_ALL);
    ASSERT_TRUE(output.motor_index == 0u);
    ASSERT_TRUE(output.speed_ref_dps == 0.0f);
    ASSERT_TRUE(context.state == GM6020_SCAN_DISARMED);
    ASSERT_TRUE(context.motor_index == 0u);
}

static void TestFirstEnabledStepWaitsWithoutMoving(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);

    const GM6020IDScanOutput output = Step(&context, true, false, 100u, 0.0f);

    ASSERT_TRUE(context.state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(context.motor_index == 0u);
    ASSERT_TRUE(output.motor_index == 0u);
    ASSERT_TRUE(output.action != GM6020_SCAN_ACTION_RUN_CURRENT);
    ASSERT_TRUE(output.speed_ref_dps == 0.0f);
}

static void TestOnlineMotorCapturesOriginAndRunsForward(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    (void)Step(&context, true, false, 100u, 0.0f);

    const GM6020IDScanOutput output = Step(&context, true, true, 120u, -35.25f);

    ASSERT_TRUE(context.state == GM6020_SCAN_FORWARD);
    ASSERT_TRUE(context.start_angle_deg == -35.25f);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_RUN_CURRENT);
    ASSERT_TRUE(output.speed_ref_dps == 10.0f);
}

static void TestTravelLimitReversesDirection(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 100u, 120u, 30.0f);

    const GM6020IDScanOutput output = Step(&context, true, true, 500u, 40.0f);

    ASSERT_TRUE(context.state == GM6020_SCAN_REVERSE);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_RUN_CURRENT);
    ASSERT_TRUE(output.speed_ref_dps == -10.0f);
}

static void TestOriginToleranceStopsAndSettles(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 100u, 120u, 30.0f);
    (void)Step(&context, true, true, 500u, 40.0f);

    const GM6020IDScanOutput output = Step(&context, true, true, 900u, 30.5f);

    ASSERT_TRUE(context.state == GM6020_SCAN_SETTLE);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_CURRENT);
    ASSERT_TRUE(output.speed_ref_dps == 0.0f);
}

static void TestOfflineTimeoutSkipsMotorAcrossTimerWrap(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    (void)Step(&context, true, false, UINT32_MAX - 499u, 0.0f);

    const GM6020IDScanOutput output = Step(&context, true, false, 500u, 0.0f);

    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_CURRENT);
    ASSERT_TRUE(output.motor_index == 0u);
    ASSERT_TRUE(context.state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(context.motor_index == 1u);
}

static void TestForwardTimeoutSkipsMotor(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 100u, 120u, 30.0f);

    const GM6020IDScanOutput output = Step(&context, true, true, 3120u, 35.0f);

    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_CURRENT);
    ASSERT_TRUE(context.state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(context.motor_index == 1u);
}

static void TestReverseTimeoutSkipsMotor(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 100u, 120u, 30.0f);
    (void)Step(&context, true, true, 200u, 40.0f);

    const GM6020IDScanOutput output = Step(&context, true, true, 3200u, 35.0f);

    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_CURRENT);
    ASSERT_TRUE(context.state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(context.motor_index == 1u);
}

static void TestSettlingAfterId7CompletesScan(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    context.state = GM6020_SCAN_SETTLE;
    context.motor_index = 6u;
    context.state_entry_ms = 1000u;

    const GM6020IDScanOutput output = Step(&context, true, true, 1300u, 0.0f);

    ASSERT_TRUE(context.state == GM6020_SCAN_COMPLETE);
    ASSERT_TRUE(context.motor_index == 6u);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_ALL);
    ASSERT_TRUE(output.speed_ref_dps == 0.0f);
}

static void TestCompleteLatchesUntilDisabled(void)
{
    GM6020IDScanLogic context;
    const GM6020IDScanLogicConfig config = DefaultConfig();
    GM6020IDScanLogicInit(&context, &config);
    context.state = GM6020_SCAN_COMPLETE;
    context.motor_index = 6u;

    GM6020IDScanOutput output = Step(&context, true, true, 2000u, 0.0f);
    ASSERT_TRUE(context.state == GM6020_SCAN_COMPLETE);
    ASSERT_TRUE(context.motor_index == 6u);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_ALL);

    output = Step(&context, false, true, 2100u, 0.0f);
    ASSERT_TRUE(context.state == GM6020_SCAN_DISARMED);
    ASSERT_TRUE(context.motor_index == 0u);
    ASSERT_TRUE(output.action == GM6020_SCAN_ACTION_STOP_ALL);

    output = Step(&context, true, false, 2200u, 0.0f);
    ASSERT_TRUE(context.state == GM6020_SCAN_WAIT_ONLINE);
    ASSERT_TRUE(context.motor_index == 0u);
    ASSERT_TRUE(output.action != GM6020_SCAN_ACTION_RUN_CURRENT);
}

static void TestRunCommandsRespectSpeedLimit(void)
{
    GM6020IDScanLogic context;
    GM6020IDScanLogicConfig config = DefaultConfig();
    config.max_speed_dps = 4.0f;
    GM6020IDScanLogicInit(&context, &config);
    ArmAndStartForward(&context, 100u, 120u, 30.0f);

    GM6020IDScanOutput output = Step(&context, true, true, 150u, 35.0f);
    ASSERT_TRUE(output.speed_ref_dps == 4.0f);
    ASSERT_TRUE(fabsf(output.speed_ref_dps) <= config.max_speed_dps);

    output = Step(&context, true, true, 200u, 40.0f);
    ASSERT_TRUE(output.speed_ref_dps == -4.0f);
    ASSERT_TRUE(fabsf(output.speed_ref_dps) <= config.max_speed_dps);
}

int main(void)
{
    TestDisabledResetsToDisarmed();
    TestFirstEnabledStepWaitsWithoutMoving();
    TestOnlineMotorCapturesOriginAndRunsForward();
    TestTravelLimitReversesDirection();
    TestOriginToleranceStopsAndSettles();
    TestOfflineTimeoutSkipsMotorAcrossTimerWrap();
    TestForwardTimeoutSkipsMotor();
    TestReverseTimeoutSkipsMotor();
    TestSettlingAfterId7CompletesScan();
    TestCompleteLatchesUntilDisabled();
    TestRunCommandsRespectSpeedLimit();

    puts("GM6020 ID scan logic tests passed.");
    return EXIT_SUCCESS;
}
