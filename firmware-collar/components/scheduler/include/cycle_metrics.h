#pragma once

#include <cstdint>
#include "board_mode.h"
#include "communication_manager.h"

namespace cownect::scheduler {

enum class CycleState {
    BOOT,
    PREPARE_CYCLE,
    POWERING_PERIPHERALS,
    CAPTURING,
    POST_CAPTURE,
    COMMUNICATION,
    POWERING_DOWN,
    WAITING_AWAKE,
    PREPARING_SLEEP,
    ERROR_RECOVERY
};

enum class CycleResult { COMPLETE, DEGRADED, FAILED, OVERRUN };
const char* cycle_result_name(CycleResult r);

// Material 10 section 10 (+ Material 3 section 25 communication metrics).
// sleep_requested_ms is the REQUESTED duration, not a measured sleep time.
struct CycleMetrics {
    uint32_t cycle_sequence;
    uint32_t capture_id;

    uint32_t capture_time_ms;
    uint32_t processing_time_ms;
    uint32_t communication_time_ms;
    uint32_t power_down_time_ms;

    uint32_t awake_elapsed_ms;
    uint32_t sleep_requested_ms;
    uint32_t cycle_overrun_ms;

    bool capture_complete;
    bool communication_implemented;
    bool communication_complete;
    bool entered_deep_sleep;
    bool peripherals_commanded_on_before_idle;

    OperationMode mode_at_cycle_start;
    OperationMode mode_before_idle;
    CycleResult result;

    comm::CommunicationMetrics comm;
};

}  // namespace cownect::scheduler
