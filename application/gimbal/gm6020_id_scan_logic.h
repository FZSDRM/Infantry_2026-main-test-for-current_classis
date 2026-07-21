#ifndef GM6020_ID_SCAN_LOGIC_H
#define GM6020_ID_SCAN_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GM6020_SCAN_DISARMED,
    GM6020_SCAN_WAIT_ONLINE,
    GM6020_SCAN_FORWARD,
    GM6020_SCAN_REVERSE,
    GM6020_SCAN_SETTLE,
    GM6020_SCAN_COMPLETE,
} GM6020IDScanState;

typedef enum {
    GM6020_SCAN_ACTION_NONE,
    GM6020_SCAN_ACTION_STOP_ALL,
    GM6020_SCAN_ACTION_STOP_CURRENT,
    GM6020_SCAN_ACTION_RUN_CURRENT,
} GM6020IDScanAction;

typedef struct {
    uint8_t motor_count;
    float max_speed_dps;
    float travel_deg;
    float origin_tolerance_deg;
    uint32_t online_timeout_ms;
    uint32_t motion_timeout_ms;
    uint32_t settle_ms;
} GM6020IDScanLogicConfig;

typedef struct {
    GM6020IDScanLogicConfig config;
    GM6020IDScanState state;
    uint8_t motor_index;
    uint32_t state_entry_ms;
    float start_angle_deg;
} GM6020IDScanLogic;

typedef struct {
    bool enabled;
    bool online;
    uint32_t now_ms;
    float angle_deg;
} GM6020IDScanInput;

typedef struct {
    GM6020IDScanAction action;
    uint8_t motor_index;
    float speed_ref_dps;
} GM6020IDScanOutput;

void GM6020IDScanLogicInit(GM6020IDScanLogic *context,
                           const GM6020IDScanLogicConfig *config);
GM6020IDScanOutput GM6020IDScanLogicStep(GM6020IDScanLogic *context,
                                         const GM6020IDScanInput *input);

#endif
