#pragma once

#include "cycle_metrics.h"
#include "esp_err.h"
#include "telemetry_record.h"

namespace cownect::scheduler {

// Test configurations are separate from production constants (Material 10 section 8).
struct SchedulerConfig {
    uint32_t cycle_target_ms;
    uint32_t capture_ms;
    bool allow_deep_sleep;                     // NORMAL mode may sleep for the remainder
    bool wait_until_boundary;                  // awake wait when not sleeping
    bool communication_enabled;
    comm::CommunicationMode communication_mode;
    uint32_t artificial_post_capture_delay_ms; // development fault injection only (test 10.8)
};

// 120 s / 30 s, communication per menuconfig (disabled until RF/network values exist).
SchedulerConfig scheduler_production_config();

// Orchestration only (Material 10): power, capture, processing, communication, cleanup,
// dynamic remaining-time calculation, sleep/awake decision. No sensor/radio protocol details.
class CycleScheduler {
public:
    esp_err_t init(const SchedulerConfig& config);
    // NORMAL mode with remaining time: enters deep sleep and does not return.
    esp_err_t run_one_cycle();

    const CycleMetrics& last_cycle_metrics() const { return metrics_; }
    const features::TelemetryRecord& last_telemetry() const { return telemetry_; }
    CycleState state() const { return state_; }

private:
    void wait_awake_until_boundary(int64_t cycle_start_us, int64_t target_us);
    void enter_sleep(uint64_t remaining_us);
    void log_summary() const;

    SchedulerConfig cfg_ = {};
    CycleMetrics metrics_ = {};
    features::TelemetryRecord telemetry_ = {};
    CycleState state_ = CycleState::BOOT;
};

CycleScheduler& cycle_scheduler();

}  // namespace cownect::scheduler
