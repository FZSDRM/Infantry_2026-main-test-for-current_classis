#include "gm6020_id_scan_logic.h"

#include <math.h>

static void ResetRuntime(GM6020IDScanLogic *context, uint32_t now_ms)
{
    context->state = GM6020_SCAN_DISARMED;
    context->motor_index = 0u;
    context->state_entry_ms = now_ms;
    context->start_angle_deg = 0.0f;
}

static bool HasElapsed(uint32_t now_ms, uint32_t start_ms, uint32_t duration_ms)
{
    return (uint32_t)(now_ms - start_ms) >= duration_ms;
}

static bool AdvanceMotor(GM6020IDScanLogic *context, uint32_t now_ms)
{
    if ((uint8_t)(context->motor_index + 1u) >= context->config.motor_count) {
        context->state = GM6020_SCAN_COMPLETE;
        return true;
    }

    context->motor_index++;
    context->state = GM6020_SCAN_WAIT_ONLINE;
    context->state_entry_ms = now_ms;
    context->start_angle_deg = 0.0f;
    return false;
}

static float ClampSpeed(const GM6020IDScanLogic *context, float speed_dps)
{
    const float limit = fabsf(context->config.max_speed_dps);
    if (speed_dps > limit) {
        return limit;
    }
    if (speed_dps < -limit) {
        return -limit;
    }
    return speed_dps;
}

static void SetRunOutput(const GM6020IDScanLogic *context,
                         GM6020IDScanOutput *output,
                         float speed_dps)
{
    output->action = GM6020_SCAN_ACTION_RUN_CURRENT;
    output->speed_ref_dps = ClampSpeed(context, speed_dps);
}

static void StopAndAdvance(GM6020IDScanLogic *context,
                           GM6020IDScanOutput *output,
                           uint32_t now_ms)
{
    output->action = GM6020_SCAN_ACTION_STOP_CURRENT;
    output->speed_ref_dps = 0.0f;
    if (AdvanceMotor(context, now_ms)) {
        output->action = GM6020_SCAN_ACTION_STOP_ALL;
    }
}

void GM6020IDScanLogicInit(GM6020IDScanLogic *context,
                           const GM6020IDScanLogicConfig *config)
{
    context->config = *config;
    ResetRuntime(context, 0u);
}

GM6020IDScanOutput GM6020IDScanLogicStep(GM6020IDScanLogic *context,
                                         const GM6020IDScanInput *input)
{
    GM6020IDScanOutput output = {
        .action = GM6020_SCAN_ACTION_NONE,
        .motor_index = context->motor_index,
        .speed_ref_dps = 0.0f,
    };

    if (!input->enabled) {
        ResetRuntime(context, input->now_ms);
        output.action = GM6020_SCAN_ACTION_STOP_ALL;
        output.motor_index = 0u;
        return output;
    }

    switch (context->state) {
        case GM6020_SCAN_DISARMED:
            context->state = GM6020_SCAN_WAIT_ONLINE;
            context->state_entry_ms = input->now_ms;
            output.action = GM6020_SCAN_ACTION_STOP_CURRENT;
            break;

        case GM6020_SCAN_WAIT_ONLINE:
            if (input->online) {
                context->start_angle_deg = input->angle_deg;
                context->state = GM6020_SCAN_FORWARD;
                context->state_entry_ms = input->now_ms;
                SetRunOutput(context, &output, context->config.max_speed_dps);
            } else if (HasElapsed(input->now_ms, context->state_entry_ms,
                                  context->config.online_timeout_ms)) {
                StopAndAdvance(context, &output, input->now_ms);
            } else {
                output.action = GM6020_SCAN_ACTION_STOP_CURRENT;
            }
            break;

        case GM6020_SCAN_FORWARD:
            if (!input->online ||
                HasElapsed(input->now_ms, context->state_entry_ms,
                           context->config.motion_timeout_ms)) {
                StopAndAdvance(context, &output, input->now_ms);
            } else if (input->angle_deg >=
                       context->start_angle_deg + context->config.travel_deg) {
                context->state = GM6020_SCAN_REVERSE;
                context->state_entry_ms = input->now_ms;
                SetRunOutput(context, &output, -context->config.max_speed_dps);
            } else {
                SetRunOutput(context, &output, context->config.max_speed_dps);
            }
            break;

        case GM6020_SCAN_REVERSE:
            if (!input->online ||
                HasElapsed(input->now_ms, context->state_entry_ms,
                           context->config.motion_timeout_ms)) {
                StopAndAdvance(context, &output, input->now_ms);
            } else if (fabsf(input->angle_deg - context->start_angle_deg) <=
                       context->config.origin_tolerance_deg) {
                context->state = GM6020_SCAN_SETTLE;
                context->state_entry_ms = input->now_ms;
                output.action = GM6020_SCAN_ACTION_STOP_CURRENT;
            } else {
                SetRunOutput(context, &output, -context->config.max_speed_dps);
            }
            break;

        case GM6020_SCAN_SETTLE:
            output.action = GM6020_SCAN_ACTION_STOP_CURRENT;
            if (HasElapsed(input->now_ms, context->state_entry_ms,
                           context->config.settle_ms) &&
                AdvanceMotor(context, input->now_ms)) {
                output.action = GM6020_SCAN_ACTION_STOP_ALL;
            }
            break;

        case GM6020_SCAN_COMPLETE:
            output.action = GM6020_SCAN_ACTION_STOP_ALL;
            break;
    }

    return output;
}
